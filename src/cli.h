#pragma once

#include <QString>
#include <QStringList>

// Commands that need no display, for scripts and coding agents. The pattern
// follows Hype's src/cli.cpp (MIT, David Heinemeier Hansson).
QString cliSummary();
bool isCliCommand(const QString &word);
int runCli(const QStringList &arguments);
