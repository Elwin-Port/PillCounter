// src/main/routes/pills.js
//
// This file defines the REST API for pill records.
// REST means each URL + HTTP method combo does one specific thing:
//
//   GET    /api/pills         → read all pills
//   GET    /api/pills/:uid    → read one pill by its NFC tag UID
//   POST   /api/pills         → create a new pill record
//   PUT    /api/pills/:id     → update an existing pill record
//   DELETE /api/pills/:id     → delete a pill record

import express from 'express'
import db from '../db/database.js'

const router = express.Router()

// ─── GET all pills ───────────────────────────────────────────────────────────
router.get('/', (req, res) => {
  try {
    const pills = db.prepare('SELECT * FROM pills ORDER BY pool_number').all()
    res.json(pills)
  } catch (err) {
    res.status(500).json({ error: err.message })
  }
})

// ─── GET one pill by NFC tag UID ─────────────────────────────────────────────
// Used when a tag is scanned — look up if this UID has been initialized before.
router.get('/uid/:uid', (req, res) => {
  try {
    const pill = db.prepare('SELECT * FROM pills WHERE tag_uid = ?').get(req.params.uid)
    if (!pill) return res.status(404).json({ error: 'Tag not found' })
    res.json(pill)
  } catch (err) {
    res.status(500).json({ error: err.message })
  }
})

// ─── POST create a new pill ──────────────────────────────────────────────────
// Called from the TagInit screen when a new tag is configured.
router.post('/', (req, res) => {
  try {
    const {
      tag_uid, pool_number, pill_name,
      dosage, schedule_time,
      quantity_max, quantity_current, low_stock_threshold
    } = req.body

    const result = db.prepare(`
      INSERT INTO pills
        (tag_uid, pool_number, pill_name, dosage, schedule_time,
         quantity_max, quantity_current, low_stock_threshold)
      VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    `).run(
      tag_uid, pool_number, pill_name,
      dosage, schedule_time,
      quantity_max ?? 30,
      quantity_current ?? quantity_max ?? 30,
      low_stock_threshold ?? 5
    )

    const newPill = db.prepare('SELECT * FROM pills WHERE id = ?').get(result.lastInsertRowid)
    res.status(201).json(newPill)
  } catch (err) {
    res.status(500).json({ error: err.message })
  }
})

// ─── PUT update a pill ───────────────────────────────────────────────────────
// Used for two things:
//   1. Editing pill info (name, dosage, schedule)
//   2. Updating the current count when the ESP32 reports a pill was taken
router.put('/:id', (req, res) => {
  try {
    const {
      pill_name, dosage, schedule_time,
      quantity_max, quantity_current, low_stock_threshold
    } = req.body

    db.prepare(`
      UPDATE pills SET
        pill_name           = COALESCE(?, pill_name),
        dosage              = COALESCE(?, dosage),
        schedule_time       = COALESCE(?, schedule_time),
        quantity_max        = COALESCE(?, quantity_max),
        quantity_current    = COALESCE(?, quantity_current),
        low_stock_threshold = COALESCE(?, low_stock_threshold),
        updated_at          = datetime('now')
      WHERE id = ?
    `).run(
      pill_name, dosage, schedule_time,
      quantity_max, quantity_current, low_stock_threshold,
      req.params.id
    )

    const updated = db.prepare('SELECT * FROM pills WHERE id = ?').get(req.params.id)
    res.json(updated)
  } catch (err) {
    res.status(500).json({ error: err.message })
  }
})

// ─── DELETE a pill ───────────────────────────────────────────────────────────
router.delete('/:id', (req, res) => {
  try {
    db.prepare('DELETE FROM pills WHERE id = ?').run(req.params.id)
    res.json({ success: true })
  } catch (err) {
    res.status(500).json({ error: err.message })
  }
})

export default router