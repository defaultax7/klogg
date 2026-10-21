/*
 * Copyright (C) 2026 Anton Filimonov and other contributors
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

#ifndef KLOGG_OPENURLDIALOG_H
#define KLOGG_OPENURLDIALOG_H

#include <QDialog>

class QCheckBox;
class QGroupBox;
class QLineEdit;
class QPushButton;
class QWidget;

// Dialog for opening a URL with optional SSH credential and host-key settings.
class OpenUrlDialog : public QDialog {
    Q_OBJECT

public:
    explicit OpenUrlDialog( QWidget* parent = nullptr );

    void setUrl( const QString& url );
    QString url() const;

public Q_SLOTS:
    void accept() override;

private Q_SLOTS:
    void updateForUrl();
    void updateCredentialState();

private:
    QString buildResultUrl() const;
    QString defaultCredentialTarget() const;

    QLineEdit* urlEdit_ = nullptr;
    QGroupBox* sshOptionsGroup_ = nullptr;
    QCheckBox* credentialCheckBox_ = nullptr;
    QWidget* credentialRow_ = nullptr;
    QLineEdit* credentialTargetEdit_ = nullptr;
    QCheckBox* disableHostKeyCheckBox_ = nullptr;
    QCheckBox* strictHostKeyCheckBox_ = nullptr;
    QString resultUrl_;
    bool wasSshUrl_ = false;
};

#endif
