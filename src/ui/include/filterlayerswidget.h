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

    // Save the current layer stack to a file. Files ending with .json are
    // written as JSON, everything else uses the application's INI format.
    bool exportLayersToFile( const QString& fileName, QString* errorString = nullptr ) const;
    // Replace the layer stack with the contents of a file. The file can be
    // either in the application's INI format or JSON. Emits layersChanged().
    bool importLayersFromFile( const QString& fileName, QString* errorString = nullptr );

    // Serialize the current layer stack to JSON and back.
    QString layersToJson() const;
    bool importLayersFromJson( const QString& json, QString* errorString = nullptr );

    // Convenience aliases kept for callers that use a shorter name.
    bool exportToFile( const QString& fileName, QString* errorString = nullptr ) const
    {
        return exportLayersToFile( fileName, errorString );
    }
    bool importFromFile( const QString& fileName, QString* errorString = nullptr )
    {
        return importLayersFromFile( fileName, errorString );
    }
    bool saveLayersToFile( const QString& fileName, QString* errorString = nullptr ) const
    {
        return exportLayersToFile( fileName, errorString );
    }
    bool loadLayersFromFile( const QString& fileName, QString* errorString = nullptr )
    {
        return importLayersFromFile( fileName, errorString );
    }

public Q_SLOTS:
    // Append a layer and notify the owner. Used by the search context menu.
    void addLayer( const QString& pattern, bool isExclude );
    // Replace the whole stack without emitting layersChanged().
    void setLayers( const QList<FilterLayer>& layers );
    // Remove all layers and notify the owner.
    void clearLayers();

    // Ask the user for a file and export the current layer stack.
    void exportLayers();
    // Ask the user for a file and replace the current layer stack with it.
    void importLayers();

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
    QToolButton* exportButton_ = nullptr;
    QToolButton* importButton_ = nullptr;
};

#endif
