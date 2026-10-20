/*
 * Copyright (C) 2024 Anton Filimonov and other contributors
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

#include "filterlayerswidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSaveFile>
#include <QSettings>
#include <QSizePolicy>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int FilterLayersFileVersion = 1;
const QString FilterLayersGroup = QStringLiteral( "FilterLayers" );

void setError( QString* errorString, const QString& message )
{
    if ( errorString != nullptr ) {
        *errorString = message;
    }
}

QList<FilterLayer> readLayersFromSettings( QSettings& settings, const QString& arrayName )
{
    QList<FilterLayer> layers;

    const auto size = settings.beginReadArray( arrayName );
    layers.reserve( size );

    for ( auto index = 0; index < size; ++index ) {
        settings.setArrayIndex( index );

        FilterLayer layer;
        layer.pattern = settings.value( "pattern", settings.value( "filter" ) ).toString();
        layer.isExclude
            = settings.value( "exclude", settings.value( "isExclude", false ) ).toBool();
        layer.isEnabled
            = settings.value( "enabled", settings.value( "isEnabled", true ) ).toBool();
        layer.followingLines
            = qMax( 0, settings.value( "followingLines", settings.value( "following", 0 ) )
                           .toInt() );

        if ( !layer.pattern.isEmpty() ) {
            layers.push_back( layer );
        }
    }

    settings.endArray();

    return layers;
}

QList<FilterLayer> readLayersFromJsonArray( const QJsonArray& layersArray )
{
    const auto toBool = []( const QJsonValue& value, bool defaultValue ) {
        if ( value.isBool() ) {
            return value.toBool();
        }
        if ( value.isDouble() ) {
            return value.toDouble() != 0;
        }
        if ( value.isString() ) {
            const auto text = value.toString().trimmed().toLower();
            if ( text == "true" || text == "1" ) {
                return true;
            }
            if ( text == "false" || text == "0" ) {
                return false;
            }
        }
        return defaultValue;
    };

    QList<FilterLayer> layers;
    layers.reserve( layersArray.size() );

    for ( const auto& value : layersArray ) {
        const auto layerObject = value.toObject();

        FilterLayer layer;
        layer.pattern = layerObject.value( "pattern" ).toString();
        if ( layer.pattern.isEmpty() ) {
            layer.pattern = layerObject.value( "filter" ).toString();
        }
        layer.isExclude = toBool( layerObject.value( "exclude" ),
                                  toBool( layerObject.value( "isExclude" ), false ) );
        layer.isEnabled = toBool( layerObject.value( "enabled" ),
                                  toBool( layerObject.value( "isEnabled" ), true ) );
        layer.followingLines = qMax(
            0, layerObject.value( "followingLines" )
                   .toInt( layerObject.value( "following" ).toInt( 0 ) ) );

        if ( !layer.pattern.isEmpty() ) {
            layers.push_back( layer );
        }
    }

    return layers;
}
} // namespace

FilterLayersWidget::FilterLayersWidget( QWidget* parent )
    : QWidget( parent )
{
    auto* mainLayout = new QVBoxLayout( this );
    mainLayout->setContentsMargins( 2, 0, 2, 0 );
    mainLayout->setSpacing( 1 );

    auto* inputLayout = new QHBoxLayout;
    inputLayout->setContentsMargins( 0, 0, 0, 0 );
    inputLayout->setSpacing( 4 );

    auto* titleLabel = new QLabel( tr( "Additional filter layers:" ), this );
    titleLabel->setToolTip(
        tr( "These layers are applied on top of the main search. Include layers keep matching "
            "lines, exclude layers remove matching lines." ) );

    patternEdit_ = new QLineEdit( this );
    patternEdit_->setPlaceholderText(
        tr( "Pattern to include or exclude from the current results" ) );
    patternEdit_->setClearButtonEnabled( true );

    typeCombo_ = new QComboBox( this );
    typeCombo_->addItem( tr( "Include" ), false );
    typeCombo_->addItem( tr( "Exclude" ), true );
    typeCombo_->setToolTip( tr( "Include narrows the results, exclude removes matching lines" ) );

    followingLinesSpinBox_ = new QSpinBox( this );
    followingLinesSpinBox_->setObjectName( "followingLinesSpinBox" );
    followingLinesSpinBox_->setRange( 0, 1000 );
    followingLinesSpinBox_->setValue( 0 );
    followingLinesSpinBox_->setPrefix( "+" );
    followingLinesSpinBox_->setSuffix( tr( " lines" ) );
    followingLinesSpinBox_->setToolTip(
        tr( "Also include or exclude this many lines following each matching line" ) );
    followingLinesSpinBox_->setKeyboardTracking( false );

    addButton_ = new QToolButton( this );
    addButton_->setText( tr( "Add" ) );
    addButton_->setAutoRaise( true );
    addButton_->setToolTip( tr( "Add this pattern as a filter layer" ) );

    clearButton_ = new QToolButton( this );
    clearButton_->setText( tr( "Clear" ) );
    clearButton_->setAutoRaise( true );
    clearButton_->setToolTip( tr( "Remove all additional filter layers" ) );

    exportButton_ = new QToolButton( this );
    exportButton_->setObjectName( "exportFilterLayersButton" );
    exportButton_->setText( tr( "Export" ) );
    exportButton_->setAutoRaise( true );
    exportButton_->setToolTip( tr( "Export the current filter layers to a file" ) );

    importButton_ = new QToolButton( this );
    importButton_->setObjectName( "importFilterLayersButton" );
    importButton_->setText( tr( "Import" ) );
    importButton_->setAutoRaise( true );
    importButton_->setToolTip( tr( "Replace the current filter layers with layers from a file" ) );

    inputLayout->addWidget( titleLabel );
    inputLayout->addWidget( patternEdit_, 1 );
    inputLayout->addWidget( typeCombo_ );
    inputLayout->addWidget( followingLinesSpinBox_ );
    inputLayout->addWidget( addButton_ );
    inputLayout->addWidget( clearButton_ );
    inputLayout->addWidget( exportButton_ );
    inputLayout->addWidget( importButton_ );

    rowsContainer_ = new QWidget( this );
    rowsLayout_ = new QVBoxLayout( rowsContainer_ );
    rowsLayout_->setContentsMargins( 8, 0, 0, 0 );
    rowsLayout_->setSpacing( 1 );

    mainLayout->addLayout( inputLayout );
    mainLayout->addWidget( rowsContainer_ );

    connect( patternEdit_, &QLineEdit::returnPressed, this,
             &FilterLayersWidget::addLayerFromInput );
    connect( addButton_, &QToolButton::clicked, this, &FilterLayersWidget::addLayerFromInput );
    connect( clearButton_, &QToolButton::clicked, this, &FilterLayersWidget::clearLayers );
    connect( exportButton_, &QToolButton::clicked, this, &FilterLayersWidget::exportLayers );
    connect( importButton_, &QToolButton::clicked, this, &FilterLayersWidget::importLayers );

    rebuildRows();
}

void FilterLayersWidget::addLayer( const QString& pattern, bool isExclude, int followingLines )
{
    if ( pattern.trimmed().isEmpty() ) {
        return;
    }

    layers_.append( FilterLayer{ pattern, isExclude, true, qMax( 0, followingLines ) } );
    rebuildRows();
    notifyChanged();
}

void FilterLayersWidget::setLayers( const QList<FilterLayer>& layers )
{
    layers_ = layers;
    rebuildRows();
}

void FilterLayersWidget::clearLayers()
{
    if ( layers_.isEmpty() ) {
        return;
    }

    layers_.clear();
    rebuildRows();
    notifyChanged();
}

QString FilterLayersWidget::layersToJson() const
{
    QJsonArray layersArray;

    for ( const auto& layer : layers_ ) {
        QJsonObject layerObject;
        layerObject.insert( "pattern", layer.pattern );
        layerObject.insert( "exclude", layer.isExclude );
        layerObject.insert( "enabled", layer.isEnabled );
        layerObject.insert( "followingLines", layer.followingLines );
        layersArray.append( layerObject );
    }

    QJsonObject root;
    root.insert( "version", FilterLayersFileVersion );
    root.insert( "layers", layersArray );

    return QString::fromUtf8( QJsonDocument( root ).toJson( QJsonDocument::Indented ) );
}

bool FilterLayersWidget::exportLayersToFile( const QString& fileName, QString* errorString ) const
{
    if ( fileName.isEmpty() ) {
        setError( errorString, tr( "File name is empty" ) );
        return false;
    }

    if ( fileName.endsWith( ".json", Qt::CaseInsensitive ) ) {
        QSaveFile file{ fileName };
        if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) ) {
            setError( errorString, file.errorString() );
            return false;
        }

        const auto fileData = layersToJson().toUtf8();
        if ( file.write( fileData ) != fileData.size() || !file.commit() ) {
            setError( errorString, file.errorString() );
            return false;
        }

        return true;
    }

    QSettings settings{ fileName, QSettings::IniFormat };
    settings.beginGroup( FilterLayersGroup );
    settings.setValue( "version", FilterLayersFileVersion );
    settings.remove( "layers" );
    settings.beginWriteArray( "layers", static_cast<int>( layers_.size() ) );

    for ( auto index = 0; index < layers_.size(); ++index ) {
        settings.setArrayIndex( index );

        const auto& layer = layers_.at( index );
        settings.setValue( "pattern", layer.pattern );
        settings.setValue( "filter", layer.pattern );
        settings.setValue( "exclude", layer.isExclude );
        settings.setValue( "isExclude", layer.isExclude );
        settings.setValue( "enabled", layer.isEnabled );
        settings.setValue( "isEnabled", layer.isEnabled );
        settings.setValue( "followingLines", layer.followingLines );
        settings.setValue( "following", layer.followingLines );
    }

    settings.endArray();
    settings.endGroup();
    settings.sync();

    if ( settings.status() != QSettings::NoError ) {
        setError( errorString, tr( "Failed to write filter layers file" ) );
        return false;
    }

    return true;
}

bool FilterLayersWidget::importLayersFromJson( const QString& json, QString* errorString )
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson( json.toUtf8(), &parseError );

    if ( parseError.error != QJsonParseError::NoError ) {
        setError( errorString, parseError.errorString() );
        return false;
    }

    QJsonArray layersArray;
    bool layersFound = false;

    if ( document.isArray() ) {
        layersArray = document.array();
        layersFound = true;
    }
    else if ( document.isObject() ) {
        const auto root = document.object();

        if ( root.value( "version" ).toInt( FilterLayersFileVersion ) > FilterLayersFileVersion ) {
            setError( errorString, tr( "Unsupported filter layers file version" ) );
            return false;
        }

        if ( root.value( "layers" ).isArray() ) {
            layersArray = root.value( "layers" ).toArray();
            layersFound = true;
        }
        else if ( root.value( "filters" ).isArray() ) {
            layersArray = root.value( "filters" ).toArray();
            layersFound = true;
        }
        else if ( root.value( FilterLayersGroup ).isObject() ) {
            const auto group = root.value( FilterLayersGroup ).toObject();
            if ( group.value( "layers" ).isArray() ) {
                layersArray = group.value( "layers" ).toArray();
                layersFound = true;
            }
            else if ( group.value( "filters" ).isArray() ) {
                layersArray = group.value( "filters" ).toArray();
                layersFound = true;
            }
        }
    }

    if ( !layersFound ) {
        setError( errorString, tr( "File does not contain filter layers" ) );
        return false;
    }

    setLayers( readLayersFromJsonArray( layersArray ) );
    notifyChanged();

    return true;
}

bool FilterLayersWidget::importLayersFromFile( const QString& fileName, QString* errorString )
{
    if ( fileName.isEmpty() ) {
        setError( errorString, tr( "File name is empty" ) );
        return false;
    }

    QFile file{ fileName };
    if ( !file.open( QIODevice::ReadOnly ) ) {
        setError( errorString, file.errorString() );
        return false;
    }

    const auto fileData = file.readAll();
    file.close();

    const auto trimmedData = fileData.trimmed();
    if ( trimmedData.startsWith( '{' )
         || fileName.endsWith( ".json", Qt::CaseInsensitive ) ) {
        return importLayersFromJson( QString::fromUtf8( trimmedData ), errorString );
    }

    QSettings settings{ fileName, QSettings::IniFormat };
    if ( settings.status() != QSettings::NoError ) {
        setError( errorString, tr( "Failed to read filter layers file" ) );
        return false;
    }

    QString groupName;
    for ( const auto& group : settings.childGroups() ) {
        if ( group == FilterLayersGroup || group == "FilterLayersCollection"
             || group == "FilterLayersWidget" ) {
            groupName = group;
            break;
        }
    }

    if ( !groupName.isEmpty() ) {
        settings.beginGroup( groupName );
    }

    QString arrayName;
    if ( settings.contains( "layers/size" ) ) {
        arrayName = "layers";
    }
    else if ( settings.contains( "filters/size" ) ) {
        arrayName = "filters";
    }

    if ( arrayName.isEmpty() ) {
        if ( !groupName.isEmpty() ) {
            settings.endGroup();
        }

        // A JSON array may have been saved without a .json extension and does
        // not start with '{' like a JSON object does.
        if ( trimmedData.startsWith( '[' ) ) {
            if ( importLayersFromJson( QString::fromUtf8( trimmedData ), errorString ) ) {
                return true;
            }
        }

        setError( errorString, tr( "File does not contain filter layers" ) );
        return false;
    }

    const auto version = settings.value( "version", FilterLayersFileVersion ).toInt();
    if ( version > FilterLayersFileVersion ) {
        if ( !groupName.isEmpty() ) {
            settings.endGroup();
        }
        setError( errorString, tr( "Unsupported filter layers file version" ) );
        return false;
    }

    const auto layers = readLayersFromSettings( settings, arrayName );

    if ( !groupName.isEmpty() ) {
        settings.endGroup();
    }

    setLayers( layers );
    notifyChanged();

    return true;
}

void FilterLayersWidget::exportLayers()
{
    auto fileName = QFileDialog::getSaveFileName(
        this, tr( "Export filter layers" ), QString(),
        tr( "Filter layers (*.conf *.json);;All files (*)" ) );

    if ( fileName.isEmpty() ) {
        return;
    }

    if ( !fileName.endsWith( ".conf", Qt::CaseInsensitive )
         && !fileName.endsWith( ".json", Qt::CaseInsensitive ) ) {
        fileName += ".conf";
    }

    QString errorString;
    if ( !exportLayersToFile( fileName, &errorString ) ) {
        QMessageBox::warning( this, tr( "Export filter layers" ),
                              errorString.isEmpty()
                                  ? tr( "Failed to export filter layers" )
                                  : errorString );
    }
}

void FilterLayersWidget::importLayers()
{
    const auto fileName = QFileDialog::getOpenFileName(
        this, tr( "Import filter layers" ), QString(),
        tr( "Filter layers (*.conf *.json);;All files (*)" ) );

    if ( fileName.isEmpty() ) {
        return;
    }

    QString errorString;
    if ( !importLayersFromFile( fileName, &errorString ) ) {
        QMessageBox::warning( this, tr( "Import filter layers" ),
                              errorString.isEmpty()
                                  ? tr( "Failed to import filter layers" )
                                  : errorString );
    }
}

void FilterLayersWidget::addLayerFromInput()
{
    const auto pattern = patternEdit_->text();
    if ( pattern.trimmed().isEmpty() ) {
        return;
    }

    const auto isExclude = typeCombo_->currentData().toBool();
    addLayer( pattern, isExclude, followingLinesSpinBox_->value() );
    patternEdit_->clear();
    patternEdit_->setFocus();
}

void FilterLayersWidget::updateLayer( int index, bool isEnabled )
{
    if ( index < 0 || index >= layers_.size() ) {
        return;
    }

    if ( layers_[ index ].isEnabled == isEnabled ) {
        return;
    }

    layers_[ index ].isEnabled = isEnabled;
    notifyChanged();
}

void FilterLayersWidget::removeLayer( int index )
{
    if ( index < 0 || index >= layers_.size() ) {
        return;
    }

    layers_.removeAt( index );
    rebuildRows();
    notifyChanged();
}

void FilterLayersWidget::rebuildRows()
{
    while ( auto* item = rowsLayout_->takeAt( 0 ) ) {
        if ( auto* widget = item->widget() ) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }

    for ( auto index = 0; index < layers_.size(); ++index ) {
        const auto& layer = layers_.at( index );

        auto* row = new QWidget( rowsContainer_ );
        auto* rowLayout = new QHBoxLayout( row );
        rowLayout->setContentsMargins( 0, 0, 0, 0 );
        rowLayout->setSpacing( 4 );

        auto* enabledCheckBox = new QCheckBox( row );
        enabledCheckBox->setChecked( layer.isEnabled );
        enabledCheckBox->setToolTip( tr( "Enable or disable this filter layer" ) );

        auto* typeLabel = new QLabel( layer.isExclude ? tr( "Exclude" ) : tr( "Include" ), row );
        if ( layer.followingLines > 0 ) {
            typeLabel->setText( typeLabel->text()
                                + tr( " +%1" ).arg( layer.followingLines ) );
            typeLabel->setToolTip( tr( "The following %1 line(s) are also %2" )
                                      .arg( layer.followingLines )
                                      .arg( layer.isExclude ? tr( "excluded" )
                                                            : tr( "included" ) ) );
        }
        auto typeFont = typeLabel->font();
        typeFont.setBold( true );
        typeLabel->setFont( typeFont );
        typeLabel->setMinimumWidth( 50 );

        auto* patternLabel = new QLabel( layer.pattern, row );
        patternLabel->setToolTip( layer.pattern );
        patternLabel->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Preferred );

        auto* removeButton = new QToolButton( row );
        removeButton->setText( QString( QChar( 0x00d7 ) ) );
        removeButton->setAutoRaise( true );
        removeButton->setToolTip( tr( "Remove this filter layer" ) );

        rowLayout->addWidget( enabledCheckBox );
        rowLayout->addWidget( typeLabel );
        rowLayout->addWidget( patternLabel, 1 );
        rowLayout->addWidget( removeButton );

        rowsLayout_->addWidget( row );

        connect( enabledCheckBox, &QCheckBox::toggled, this,
                 [ this, index ]( bool checked ) { updateLayer( index, checked ); } );
        connect( removeButton, &QToolButton::clicked, this,
                 [ this, index ]() { removeLayer( index ); } );
    }

    rowsContainer_->setVisible( !layers_.isEmpty() );
    clearButton_->setEnabled( !layers_.isEmpty() );
    exportButton_->setEnabled( !layers_.isEmpty() );
}

void FilterLayersWidget::notifyChanged()
{
    Q_EMIT layersChanged();
}
