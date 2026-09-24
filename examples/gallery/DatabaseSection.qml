// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    id: section
    title: "Local database workflows"
    note: "FormDesigner · DatabaseSchemaView · ReportDesigner · SQLite"

    Tk.LocalDatabaseProvider {
        id: database
        Component.onCompleted: {
            openMemory()
            executeMigration("CREATE TABLE teams(id INTEGER PRIMARY KEY, name TEXT NOT NULL)")
            executeMigration("CREATE TABLE people(id INTEGER PRIMARY KEY, name TEXT NOT NULL, team_id INTEGER REFERENCES teams(id))")
            execute("INSERT INTO teams(name) VALUES(?)", ["Design"])
            execute("INSERT INTO people(name, team_id) VALUES(?, ?)", ["Sam Rivera", 1])
        }
    }

    Tk.Flex {
        direction: Tk.Flex.Row
        wrap: Tk.Flex.Wrap
        gap: Tk.Theme.space.md

        Tk.FormDesigner {
            width: 420
            provider: database
            tableName: "people"
            mode: "edit"
            recordId: "1"
            record: ({ id: 1, name: "Sam Rivera", team_id: 1 })
            design: ({ title: "Person", fields: [
                { fieldName: "name", label: "Name" },
                { fieldName: "team_id", label: "Team" }
            ] })
        }
        Tk.DatabaseSchemaView {
            width: 420
            provider: database
            selectedTable: "people"
            migrationSql: "CREATE INDEX idx_people_name ON people(name)"
        }
    }

    Tk.ReportDesigner {
        width: 900
        provider: database
        tableName: "people"
        groupField: "team_id"
        pageSize: 20
        Component.onCompleted: Qt.callLater(runPreview)
    }
}
