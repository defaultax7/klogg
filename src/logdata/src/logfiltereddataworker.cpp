/*
 * Copyright (C) 2009, 2010 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <memory>
#include <qsemaphore.h>
#include <utility>

#include <robin_hood.h>
#include <tbb/flow_graph.h>
#include <vector>

#include "configuration.h"
#include "dispatch_to.h"
#include "issuereporter.h"
#include "linetypes.h"
#include "log.h"
#include "progress.h"
#include "runnable_lambda.h"

#include "logdata.h"
#include "regularexpression.h"

#include "logfiltereddataworker.h"
#include "synchronization.h"

namespace {
struct PartialSearchResults {
    PartialSearchResults() = default;

    PartialSearchResults( const PartialSearchResults& ) = delete;
    PartialSearchResults( PartialSearchResults&& ) = default;
    PartialSearchResults& operator=( const PartialSearchResults& ) = delete;
    PartialSearchResults& operator=( PartialSearchResults&& ) = default;

    SearchResultArray matchingLines;
    LineLength maxLength;

    LineNumber chunkStart;
    LinesCount processedLines;
};

struct SearchBlockData {
    SearchBlockData() = default;
    SearchBlockData( LineNumber rawStart, LineNumber chunkStart, LinesCount chunkLines,
                     LogData::RawLines blockLines )
        : rawStart( rawStart )
        , chunkStart( chunkStart )
        , chunkLines( chunkLines )
        , lines( std::move( blockLines ) )
    {
    }

    SearchBlockData( const SearchBlockData& ) = delete;
    SearchBlockData( SearchBlockData&& ) = default;

    SearchBlockData& operator=( const SearchBlockData& ) = delete;
    SearchBlockData& operator=( SearchBlockData&& ) = default;

    LineNumber rawStart;
    LineNumber chunkStart;
    LinesCount chunkLines;
    LogData::RawLines lines;

    PartialSearchResults searchResults;
};

// Matches a line against a stack of filters. Every layer must accept the line:
// include layers must match, exclude layers must not match. A layer can also
// mark a number of lines following a match as matched, which makes it possible
// to include or exclude a whole stack trace after its first line.
class LayeredPatternMatcher {
public:
    explicit LayeredPatternMatcher( const FilterLayers& layers )
    {
        matchers_.reserve( layers.size() );

        for ( const auto& layer : layers ) {
            if ( layer.pattern.isEmpty() ) {
                continue;
            }

            // Build a matcher for the raw pattern and apply include/exclude
            // semantics ourselves, so that following lines can be handled for
            // exclude layers as well.
            auto matchPattern = layer;
            matchPattern.isExclude = false;

            RegularExpression expression{ matchPattern };
            const auto followingLines = std::max( 0, layer.followingLines );
            matchers_.push_back( LayerMatcher{ expression.createMatcher(), layer.isExclude,
                                               followingLines } );
            maxFollowingLines_ = std::max( maxFollowingLines_, followingLines );
        }
    }

    LayeredPatternMatcher( const LayeredPatternMatcher& ) = delete;
    LayeredPatternMatcher& operator=( const LayeredPatternMatcher& ) = delete;

    LayeredPatternMatcher( LayeredPatternMatcher&& ) = default;
    LayeredPatternMatcher& operator=( LayeredPatternMatcher&& ) = default;

    std::size_t layerCount() const
    {
        return matchers_.size();
    }

    int maxFollowingLines() const
    {
        return maxFollowingLines_;
    }

    bool rawMatch( std::size_t layer, std::string_view line ) const
    {
        return matchers_[ layer ].matcher->hasMatch( line );
    }

    bool isExclude( std::size_t layer ) const
    {
        return matchers_[ layer ].isExclude;
    }

    int followingLines( std::size_t layer ) const
    {
        return matchers_[ layer ].followingLines;
    }

private:
    struct LayerMatcher {
        std::unique_ptr<PatternMatcher> matcher;
        bool isExclude = false;
        int followingLines = 0;
    };

    klogg::vector<LayerMatcher> matchers_;
    int maxFollowingLines_ = 0;
};

LinesCount maxFollowingLines( const FilterLayers& layers )
{
    auto maxLines = 0;

    for ( const auto& layer : layers ) {
        if ( !layer.pattern.isEmpty() ) {
            maxLines = std::max( maxLines, std::max( 0, layer.followingLines ) );
        }
    }

    return LinesCount{ static_cast<LinesCount::UnderlyingType>( maxLines ) };
}

PartialSearchResults filterLines( const LayeredPatternMatcher& matcher,
                                  const LogData::RawLines& rawLines, LineNumber rawStart,
                                  LineNumber reportStart, LinesCount reportLines )
{
    LOG_DEBUG << "Filter lines at " << reportStart;
    PartialSearchResults results;
    results.chunkStart = reportStart;
    results.processedLines = reportLines;

    const auto& lines = rawLines.buildUtf8View();
    const auto layerCount = matcher.layerCount();
    if ( layerCount == 0 ) {
        return results;
    }

    const auto reportEnd = reportStart + reportLines;

    // Number of following lines still matched by each layer. The raw region
    // starts a few lines before the reported chunk so context state carries
    // correctly across chunk boundaries.
    klogg::vector<int> remainingFollowingLines( layerCount, 0 );

    for ( auto offset = 0u; offset < lines.size(); ++offset ) {
        const auto& line = lines[ offset ];
        const auto lineNumber = rawStart + LinesCount{ offset };
        auto lineAccepted = true;

        for ( auto layer = 0u; layer < layerCount; ++layer ) {
            const auto hasRawMatch = matcher.rawMatch( layer, line );
            const auto hasFollowingContext = remainingFollowingLines[ layer ] > 0;
            const auto layerMatched = hasRawMatch || hasFollowingContext;
            const auto layerAccepted = matcher.isExclude( layer ) ? !layerMatched : layerMatched;

            if ( hasRawMatch ) {
                remainingFollowingLines[ layer ] = matcher.followingLines( layer );
            }
            else if ( hasFollowingContext ) {
                --remainingFollowingLines[ layer ];
            }

            lineAccepted = lineAccepted && layerAccepted;
        }

        if ( lineAccepted && lineNumber >= reportStart && lineNumber < reportEnd ) {
            results.maxLength = qMax( results.maxLength, getUntabifiedLength( line ) );
            results.matchingLines.add( lineNumber.get() );
        }
    }

    return results;
}

} // namespace

SearchResults SearchData::takeCurrentResults() const
{
    UniqueLock lock( dataMutex_ );
    return SearchResults{ std::exchange( newMatches_, {} ), maxLength_, nbLinesProcessed_ };
}

void SearchData::addAll( LineLength length, const SearchResultArray& matches,
                         LinesCount matchedLines, LinesCount processedLines )
{
    UniqueLock lock( dataMutex_ );

    maxLength_ = qMax( maxLength_, length );
    nbLinesProcessed_ = qMax( nbLinesProcessed_, processedLines );
    nbMatches_ += matchedLines;

    newMatches_ |= matches;
}

LinesCount SearchData::getNbMatches() const
{
    SharedLock lock( dataMutex_ );
    return nbMatches_;
}

LineNumber SearchData::getLastProcessedLine() const
{
    SharedLock lock( dataMutex_ );
    return LineNumber{ nbLinesProcessed_.get() };
}

void SearchData::deleteMatch( LineNumber line )
{
    UniqueLock lock( dataMutex_ );
    matches_.remove( line.get() );
}

void SearchData::clear()
{
    UniqueLock locker( dataMutex_ );

    maxLength_ = LineLength( 0 );
    nbLinesProcessed_ = LinesCount( 0 );
    nbMatches_ = LinesCount( 0 );
    matches_ = {};
    newMatches_ = {};
}

LogFilteredDataWorker::LogFilteredDataWorker( const LogData& sourceLogData )
    : sourceLogData_( sourceLogData )
{
    operationsPool_.setMaxThreadCount( 1 );
}

LogFilteredDataWorker::~LogFilteredDataWorker() noexcept
{
    try {
        interruptRequested_.set();
        ScopedLock locker( operationsMutex_ );
        operationsPool_.waitForDone();
        LOG_INFO << "LogFilteredDataWorker shutdown";
    } catch ( const std::exception& e ) {
        LOG_ERROR << "Failed to destroy LogFilteredDataWorker: " << e.what();
    }
}

void LogFilteredDataWorker::connectSignalsAndRun( SearchOperation* operationRequested )
{
    connect( operationRequested, &SearchOperation::searchProgressed, this,
             &LogFilteredDataWorker::searchProgressed );
    connect( operationRequested, &SearchOperation::searchFinished, this,
             &LogFilteredDataWorker::searchFinished, Qt::QueuedConnection );

    operationRequested->run( searchData_ );
    operationRequested->disconnect( this );
}

void LogFilteredDataWorker::search( const FilterLayers& patterns, LineNumber startLine,
                                    LineNumber endLine )
{
    ScopedLock locker( operationsMutex_ ); // to protect operationRequested_
    operationsPool_.waitForDone();
    interruptRequested_.clear();

    LOG_INFO << "Search requested with " << patterns.size() << " filter layer(s)";
    QSemaphore operationStarted;
    operationsPool_.start(
        createRunnable( [ this, &operationStarted, patterns, startLine, endLine ] {
            operationStarted.release();
            ScopedLock operationLock( operationsMutex_ );
            auto operationRequested = std::make_unique<FullSearchOperation>(
                sourceLogData_, interruptRequested_, patterns, startLine, endLine );
            connectSignalsAndRun( operationRequested.get() );
        } ) );
    operationStarted.acquire();
}

void LogFilteredDataWorker::updateSearch( const FilterLayers& patterns, LineNumber startLine,
                                          LineNumber endLine, LineNumber position )
{
    ScopedLock locker( operationsMutex_ ); // to protect operationRequested_
    operationsPool_.waitForDone();
    interruptRequested_.clear();

    LOG_INFO << "Search update requested from " << position.get();

    QSemaphore operationStarted;
    operationsPool_.start(
        createRunnable( [ this, &operationStarted, patterns, startLine, endLine, position ] {
            operationStarted.release();
            ScopedLock operationLock( operationsMutex_ );
            auto operationRequested = std::make_unique<UpdateSearchOperation>(
                sourceLogData_, interruptRequested_, patterns, startLine, endLine, position );
            connectSignalsAndRun( operationRequested.get() );
        } ) );

    operationStarted.acquire();
}

void LogFilteredDataWorker::interrupt()
{
    LOG_INFO << "Search interruption requested";
    interruptRequested_.set();
}

// This will do an atomic copy of the object
SearchResults LogFilteredDataWorker::getSearchResults() const
{
    return searchData_.takeCurrentResults();
}

//
// Operations implementation
//

SearchOperation::SearchOperation( const LogData& sourceLogData, AtomicFlag& interruptRequested,
                                  const FilterLayers& patterns, LineNumber startLine,
                                  LineNumber endLine )

    : interruptRequested_( interruptRequested )
    , patterns_( patterns )
    , sourceLogData_( sourceLogData )
    , startLine_( startLine )
    , endLine_( endLine )

{
}

void SearchOperation::doSearch( SearchData& searchData, LineNumber initialLine )
{
    const auto nbSourceLines = sourceLogData_.getNbLine();

    LOG_INFO << "Searching from line " << initialLine << " to " << nbSourceLines << " with "
             << patterns_.size() << " filter layer(s)";

    using namespace std::chrono;
    high_resolution_clock::time_point t1 = high_resolution_clock::now();

    const auto& config = Configuration::get();
    const auto matchingThreadsCount = static_cast<uint32_t>( [ &config ]() {
        if ( !config.useParallelSearch() ) {
            return 1;
        }
        const auto configuredThreadPoolSize = config.searchThreadPoolSize();
        return qMax( 1, configuredThreadPoolSize == 0 ? tbb::info::default_concurrency()
                                                      : configuredThreadPoolSize );
    }() );

    LOG_INFO << "Using " << matchingThreadsCount << " matching threads";

    tbb::flow::graph searchGraph;

    if ( initialLine < startLine_ ) {
        initialLine = startLine_;
    }

    const auto endLine = qMin( LineNumber( nbSourceLines.get() ), endLine_ );
    const auto nbLinesInChunk = LinesCount(
        static_cast<LinesCount::UnderlyingType>( config.searchReadBufferSizeLines() ) );
    const auto maxContextLines = maxFollowingLines( patterns_ );

    std::chrono::microseconds fileReadingDuration{ 0 };

    using BlockDataType = SearchBlockData*;
    auto blockPrefetcher
        = tbb::flow::limiter_node<BlockDataType>( searchGraph, matchingThreadsCount * 3 );

    auto lineBlocksQueue = tbb::flow::buffer_node<BlockDataType>( searchGraph );

    using RegexMatcherNode
        = tbb::flow::function_node<BlockDataType, BlockDataType, tbb::flow::rejecting>;

    using MatcherContext = std::tuple<LayeredPatternMatcher, microseconds, RegexMatcherNode>;

    klogg::vector<MatcherContext> regexMatchers;
    regexMatchers.reserve( matchingThreadsCount );
    for ( auto index = 0u; index < matchingThreadsCount; ++index ) {
        regexMatchers.emplace_back(
            LayeredPatternMatcher{ patterns_ }, microseconds{ 0 },
            RegexMatcherNode(
                searchGraph, 1, [ &regexMatchers, index, this ]( const BlockDataType& blockData ) {
                    if ( interruptRequested_ ) {
                        LOG_INFO << "Matcher " << index << " interrupted";
                        auto results = std::make_shared<PartialSearchResults>();
                        blockData->searchResults.chunkStart = blockData->chunkStart;
                        blockData->searchResults.processedLines = blockData->chunkLines;
                        return blockData;
                    }

                    const auto& matcher
                        = std::get<LayeredPatternMatcher>( regexMatchers.at( index ) );
                    const auto matchStartTime = high_resolution_clock::now();

                    blockData->searchResults
                        = filterLines( matcher, blockData->lines, blockData->rawStart,
                                       blockData->chunkStart, blockData->chunkLines );

                    const auto matchEndTime = high_resolution_clock::now();

                    microseconds& matchDuration
                        = std::get<microseconds>( regexMatchers.at( index ) );
                    matchDuration += duration_cast<microseconds>( matchEndTime - matchStartTime );
                    LOG_DEBUG << "Searcher " << index << " block " << blockData->chunkStart
                              << " sending matches "
                              << blockData->searchResults.matchingLines.cardinality() << " from "
                              << matcher.layerCount() << " filter layer(s)";
                    return blockData;
                } ) );
    }

    auto resultsQueue = tbb::flow::buffer_node<BlockDataType>( searchGraph );

    const auto totalLines = endLine - initialLine;
    LinesCount totalProcessedLines = 0_lcount;
    LineLength maxLength = 0_length;
    LinesCount nbMatches = searchData.getNbMatches();
    auto reportedMatches = nbMatches;
    int reportedPercentage = 0;

    std::chrono::microseconds matchCombiningDuration{ 0 };

    auto matchProcessor
        = tbb::flow::function_node<BlockDataType, tbb::flow::continue_msg, tbb::flow::rejecting>(
            searchGraph, 1, [ & ]( const BlockDataType& blockData ) {
                if ( interruptRequested_ ) {
                    LOG_INFO << "Match processor interrupted";
                    return tbb::flow::continue_msg{};
                }

                const auto& matchResults = blockData->searchResults;

                const auto matchProcessorStartTime = high_resolution_clock::now();

                if ( matchResults.processedLines.get() ) {

                    maxLength = qMax( maxLength, matchResults.maxLength );
                    const LinesCount matchesCount
                        = LinesCount( matchResults.matchingLines.cardinality() );
                    nbMatches += matchesCount;

                    const auto processedLines = LinesCount{ matchResults.chunkStart.get()
                                                            + matchResults.processedLines.get() };

                    totalProcessedLines += matchResults.processedLines;

                    // After each block, copy the data to shared data
                    // and update the client
                    searchData.addAll( maxLength, matchResults.matchingLines, matchesCount,
                                       processedLines );

                    LOG_DEBUG << "done Searching chunk starting at " << matchResults.chunkStart
                              << ", " << matchResults.processedLines << " lines read.";
                }

                delete blockData;

                const auto matchProcessorEndTime = high_resolution_clock::now();
                matchCombiningDuration += duration_cast<microseconds>( matchProcessorEndTime
                                                                       - matchProcessorStartTime );
                const int percentage
                    = calculateProgress( totalProcessedLines.get(), totalLines.get() );

                if ( percentage > reportedPercentage || nbMatches > reportedMatches ) {

                    Q_EMIT searchProgressed( nbMatches, std::min( 99, percentage ), initialLine );

                    reportedPercentage = percentage;
                    reportedMatches = nbMatches;
                }

                return tbb::flow::continue_msg{};
            } );

    tbb::flow::make_edge( blockPrefetcher, lineBlocksQueue );

    for ( auto& regexMatcher : regexMatchers ) {
        tbb::flow::make_edge( lineBlocksQueue, std::get<RegexMatcherNode>( regexMatcher ) );
        tbb::flow::make_edge( std::get<RegexMatcherNode>( regexMatcher ), resultsQueue );
    }

    tbb::flow::make_edge( resultsQueue, matchProcessor );
    tbb::flow::make_edge( matchProcessor, blockPrefetcher.decrementer() );

    auto chunkStart = initialLine;
    while ( chunkStart < endLine && !interruptRequested_ ) {
        const auto lineSourceStartTime = high_resolution_clock::now();
        LOG_DEBUG << "Reading chunk starting at " << chunkStart;

        const auto linesInChunk
            = LinesCount( qMin( nbLinesInChunk.get(), ( endLine - chunkStart ).get() ) );

        // Read a bit before the reported chunk so a match just before the
        // chunk boundary can still include or exclude lines in this chunk.
        const auto rawStart = chunkStart - maxContextLines;
        const auto rawLinesCount = ( chunkStart + linesInChunk ) - rawStart;
        auto lines = sourceLogData_.getLinesRaw( rawStart, rawLinesCount );

        /*LOG_DEBUG << "Sending chunk starting at " << chunkStart << ", " <<
            lines.second.size()
                << " lines read.";*/
        BlockDataType blockData
            = new SearchBlockData{ rawStart, chunkStart, linesInChunk, std::move( lines ) };

        const auto lineSourceEndTime = high_resolution_clock::now();
        const auto chunkReadTime
            = duration_cast<microseconds>( lineSourceEndTime - lineSourceStartTime );

        /*LOG_DEBUG << "Sent chunk starting at " << chunkStart << ", " <<
        blockData->lines.second.size()
                << " lines read in " << static_cast<float>( chunkReadTime.count() )
        / 1000.f
                << " ms";*/

        chunkStart = chunkStart + nbLinesInChunk;
        fileReadingDuration += chunkReadTime;

        while ( !blockPrefetcher.try_put( blockData ) && !interruptRequested_ ) {
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
    }

    searchGraph.wait_for_all();

    high_resolution_clock::time_point t2 = high_resolution_clock::now();
    const auto durationUs = duration_cast<microseconds>( t2 - t1 );
    const auto durationMs = duration_cast<milliseconds>( t2 - t1 );

    LOG_INFO << "Searching done, overall duration " << durationUs;
    LOG_INFO << "Line reading took " << fileReadingDuration;
    LOG_INFO << "Results combining took " << matchCombiningDuration;

    for ( const auto& regexMatcher : regexMatchers ) {
        LOG_INFO << "Matching took " << std::get<microseconds>( regexMatcher );
    }

    const auto totalFileSize = sourceLogData_.getFileSize();

    LOG_INFO << "Searching perf "
             << static_cast<uint64_t>(
                    std::floor( 1000.f * static_cast<float>( ( endLine - initialLine ).get() )
                                / static_cast<float>( durationMs.count() ) ) )
             << " lines/s";
    LOG_INFO << "Searching io perf "
             << ( 1000.f * static_cast<float>( totalFileSize )
                  / static_cast<float>( durationMs.count() ) )
                    / ( 1024 * 1024 )
             << " MiB/s";

    Q_EMIT searchProgressed( nbMatches, 100, initialLine );
    Q_EMIT searchFinished();
}

// Called in the worker thread's context
void FullSearchOperation::run( SearchData& searchData )
{
    try {
        // Clear the shared data
        searchData.clear();
        doSearch( searchData, 0_lnum );
    } catch ( const std::exception& err ) {
        const auto errorString = QString( "FullSearchOperation failed: %1" ).arg( err.what() );
        LOG_ERROR << errorString;
        dispatchToMainThread( [ errorString ]() {
            IssueReporter::askUserAndReportIssue( IssueTemplate::Exception, errorString );
        } );
        searchData.clear();
    }
}

// Called in the worker thread's context
void UpdateSearchOperation::run( SearchData& searchData )
{
    try {
        auto initialLine = qMax( searchData.getLastProcessedLine(), initialPosition_ );

        if ( initialLine.get() >= 1 ) {
            // We need to re-search the last line because it might have
            // been updated (if it was not LF-terminated)
            --initialLine;
            // In case the last line matched, we don't want it to match twice.
            searchData.deleteMatch( initialLine );
        }

        doSearch( searchData, initialLine );
    } catch ( const std::exception& err ) {
        const auto errorString = QString( "UpdateSearchOpertaion failed: %1" ).arg( err.what() );
        LOG_ERROR << errorString;
        dispatchToMainThread( [ errorString ]() {
            IssueReporter::askUserAndReportIssue( IssueTemplate::Exception, errorString );
        } );
        searchData.clear();
    }
}
