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
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

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

    addButton_ = new QToolButton( this );
    addButton_->setText( tr( "Add" ) );
    addButton_->setAutoRaise( true );
    addButton_->setToolTip( tr( "Add this pattern as a filter layer" ) );

    clearButton_ = new QToolButton( this );
    clearButton_->setText( tr( "Clear" ) );
    clearButton_->setAutoRaise( true );
    clearButton_->setToolTip( tr( "Remove all additional filter layers" ) );

    inputLayout->addWidget( titleLabel );
    inputLayout->addWidget( patternEdit_, 1 );
    inputLayout->addWidget( typeCombo_ );
    inputLayout->addWidget( addButton_ );
    inputLayout->addWidget( clearButton_ );

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

    rebuildRows();
}

void FilterLayersWidget::addLayer( const QString& pattern, bool isExclude )
{
    if ( pattern.trimmed().isEmpty() ) {
        return;
    }

    layers_.append( FilterLayer{ pattern, isExclude, true } );
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

void FilterLayersWidget::addLayerFromInput()
{
    const auto pattern = patternEdit_->text();
    if ( pattern.trimmed().isEmpty() ) {
        return;
    }

    const auto isExclude = typeCombo_->currentData().toBool();
    addLayer( pattern, isExclude );
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
}

void FilterLayersWidget::notifyChanged()
{
    Q_EMIT layersChanged();
}
