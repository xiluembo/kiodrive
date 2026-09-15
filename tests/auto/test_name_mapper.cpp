/*
 * SPDX-FileCopyrightText: 2026 KIODrive authors
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This file is part of KIODrive.
 *
 * KIODrive is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 */

#include "kio/name_mapper.h"

#include <QTest>

using namespace KIODrive;

class TestNameMapper : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void reservedName()
    {
        NameMapper mapper;
        const NameMapping mapping = mapper.mapToWindows(QStringLiteral("CON"), {});
        QVERIFY(mapping.windowsName.compare(QStringLiteral("CON"), Qt::CaseInsensitive) != 0);
    }

    void illegalCharacters()
    {
        NameMapper mapper;
        const NameMapping mapping = mapper.mapToWindows(QStringLiteral("a:b*c?.txt"), {});
        QVERIFY(!mapping.windowsName.contains(QLatin1Char(':')));
        QVERIFY(!mapping.windowsName.contains(QLatin1Char('*')));
        QVERIFY(!mapping.windowsName.contains(QLatin1Char('?')));
    }

    void caseCollision()
    {
        NameMapper mapper;
        QSet<QString> existing{QStringLiteral("Readme")};
        const NameMapping mapping = mapper.mapToWindows(QStringLiteral("README"), existing);
        QVERIFY(mapping.windowsName.compare(QStringLiteral("Readme"), Qt::CaseInsensitive) != 0);
    }
};

QTEST_GUILESS_MAIN(TestNameMapper)
#include "test_name_mapper.moc"
