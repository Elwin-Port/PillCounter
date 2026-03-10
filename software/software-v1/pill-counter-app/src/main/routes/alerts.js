// src/main/routes/alerts.js
//
// REST API for alert records:
//
//   GET  /api/alerts          → all unacknowledged alerts
//   POST /api/alerts          → create a new alert (called by the alert engine)
//   PUT  /api/alerts/:id/ack  → mark one alert as dismissed

import express from 'express'
import db from '../db/database.js'

const router = express.Router()

// ─── GET all active (unacknowledged) alerts ───────────────────────────────────
router.get('/', (req, res) => {
  try {
    const alerts = db.prepare(`
      SELECT alerts.*, pills.pill_name, pills.pool_number
      FROM alerts
      JOIN pills ON alerts.pill_id = pills.id
      WHERE alerts.acknowledged = 0
      ORDER BY alerts.triggered_at DESC
    `).all()
    res.json(alerts)
  } catch (err) {
    res.status(500).json({ error: err.message })
  }
})

// ─── POST create a new alert ─────────────────────────────────────────────────
router.post('/', (req, res) => {
  try {
    const { pill_id, alert_type } = req.body
    const result = db.prepare(`
      INSERT INTO alerts (pill_id, alert_type) VALUES (?, ?)
    `).run(pill_id, alert_type)

    const newAlert = db.prepare('SELECT * FROM alerts WHERE id = ?').get(result.lastInsertRowid)
    res.status(201).json(newAlert)
  } catch (err) {
    res.status(500).json({ error: err.message })
  }
})

// ─── PUT acknowledge (dismiss) an alert ──────────────────────────────────────
router.put('/:id/ack', (req, res) => {
  try {
    db.prepare('UPDATE alerts SET acknowledged = 1 WHERE id = ?').run(req.params.id)
    res.json({ success: true })
  } catch (err) {
    res.status(500).json({ error: err.message })
  }
})

export default router