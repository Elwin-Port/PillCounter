// src/main/server.js
//
// This file creates and starts the Express web server.
// It runs inside the Electron main process — not in a browser.
//
// The server does two jobs:
//   1. Provides the /api/pills and /api/alerts endpoints for the React frontend
//   2. Will later serve the dashboard to other computers on the network (Phase 7)

import express from 'express'
import cors from 'cors'
import pillsRouter from './routes/pills.js'
import alertsRouter from './routes/alerts.js'

const app = express()

// ─── Middleware ──────────────────────────────────────────────────────────────
// Middleware runs on every request before it reaches a route handler.

// cors() allows requests from other origins (needed for remote dashboard later)
app.use(cors())

// express.json() parses the request body as JSON automatically
// Without this, req.body would be undefined in POST/PUT routes
app.use(express.json())

// ─── Routes ──────────────────────────────────────────────────────────────────
app.use('/api/pills',  pillsRouter)
app.use('/api/alerts', alertsRouter)

// ─── Health check ────────────────────────────────────────────────────────────
// A simple endpoint to confirm the server is running.
// Visit http://localhost:3001/health in a browser to test.
app.get('/health', (req, res) => res.json({ status: 'ok' }))

// ─── Start listening ─────────────────────────────────────────────────────────
const PORT = 3001

export function startServer() {
  app.listen(PORT, () => {
    console.log(`[Server] Express running at http://localhost:${PORT}`)
  })
}

export default app