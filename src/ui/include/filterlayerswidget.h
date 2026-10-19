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

#ifndef FILTERLAYERSWIDGET_H
#define FILTERLAYERSWIDGET_H

#include <QList>
#include <QString>
#include <QWidget>

class QComboBox;
class QLineEdit;
class QToolButton;
class QVBoxLayout;

// One additional filter applied after the main search.
struct FilterLayer {
    QString pattern;
    bool isExclude = false;
    bool isEnabled = true;
};

// Small editor listing the additional filter layers applied to the current
// search. Layers are evaluated in order: include layers keep matching lines,
// exclude layers remove matching lines.
class FilterLayersWidget : public QWidget {
    Q_OBJECT

public:
    explicit FilterLayersWidget( QWidget* parent = nullptr );

    const QList<FilterLayer>& layers() const
    {
        return layers_;
    }

public Q_SLOTS:
    // Append a layer and notify the owner. Used by the search context menu.
    void addLayer( const QString& pattern, bool isExclude );
    // Replace the whole stack without emitting layersChanged().
    void setLayers( const QList<FilterLayer>& layers );
    // Remove all layers and notify the owner.
    void clearLayers();

Q_SIGNALS:
    void layersChanged();

private Q_SLOTS:
    void addLayerFromInput();
    void updateLayer( int index, bool isEnabled );
    void removeLayer( int index );

private:
    void rebuildRows();
    void notifyChanged();

    QList<FilterLayer> layers_;

    QWidget* rowsContainer_ = nullptr;
    QVBoxLayout* rowsLayout_ = nullptr;
    QLineEdit* patternEdit_ = nullptr;
    QComboBox* typeCombo_ = nullptr;
    QToolButton* addButton_ = nullptr;
    QToolButton* clearButton_ = nullptr;
};

#endif
