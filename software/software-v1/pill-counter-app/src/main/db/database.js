// src/main/db/database.js
//
// Opens (or creates) the SQLite database file and ensures
// all tables exist. The schema is defined inline as a string
// so electron-vite doesn't need to copy any extra files.

import Database from 'better-sqlite3'
import { app } from 'electron'
import path from 'path'

// ─── Decide where to store the database file ────────────────────────────────
// app.getPath('userData') on Windows returns:
//   C:\Users\YourName\AppData\Roaming\pill-counter-app
const userDataPath = app.getPath('userData')
const dbPath = path.join(userDataPath, 'pillcounter.db')

console.log('[DB] Database location:', dbPath)

// ─── Open the database ───────────────────────────────────────────────────────
const db = new Database(dbPath)

// ─── Schema (inline) ─────────────────────────────────────────────────────────
// Defining the schema here avoids file-not-found issues when electron-vite
// compiles the main process into the out/ folder.
const schema = `
  CREATE TABLE IF NOT EXISTS pills (
    id                   INTEGER PRIMARY KEY AUTOINCREMENT,
    tag_uid              TEXT    NOT NULL,
    pool_number          INTEGER NOT NULL,
    pill_name            TEXT    NOT NULL,
    dosage               TEXT,
    schedule_time        TEXT,
    quantity_max         INTEGER DEFAULT 30,
    quantity_current     INTEGER DEFAULT 30,
    low_stock_threshold  INTEGER DEFAULT 5,
    created_at           TEXT    DEFAULT (datetime('now')),
    updated_at           TEXT    DEFAULT (datetime('now'))
  );

  CREATE TABLE IF NOT EXISTS alerts (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    pill_id        INTEGER NOT NULL REFERENCES pills(id),
    alert_type     TEXT    NOT NULL,
    triggered_at   TEXT    DEFAULT (datetime('now')),
    acknowledged   INTEGER DEFAULT 0
  );
`

db.exec(schema)
console.log('[DB] Tables ready.')

export default db