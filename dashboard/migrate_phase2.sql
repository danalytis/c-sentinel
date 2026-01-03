-- C-Sentinel Dashboard Migration: Phase 2
-- Adds audit_events table for event history with timestamps
-- 
-- Usage: sudo -u postgres psql -d sentinel -f migrate_phase2.sql

BEGIN;

-- Create audit_events table for historical tracking
CREATE TABLE IF NOT EXISTS audit_events (
    id SERIAL PRIMARY KEY,
    host_id INTEGER REFERENCES hosts(id) ON DELETE CASCADE,
    fingerprint_id INTEGER REFERENCES fingerprints(id) ON DELETE CASCADE,
    captured_at TIMESTAMP DEFAULT NOW(),
    event_type VARCHAR(32) NOT NULL,  -- 'auth_failure', 'sudo', 'file_access', 'brute_force', 'selinux_denial', 'apparmor_denial'
    count INTEGER DEFAULT 1,
    details JSONB,  -- Additional context: usernames, file paths, processes, etc.
    acknowledged BOOLEAN DEFAULT FALSE,
    acknowledged_at TIMESTAMP
);

-- Indexes for efficient querying
CREATE INDEX IF NOT EXISTS idx_audit_events_host_time 
    ON audit_events(host_id, captured_at DESC);

CREATE INDEX IF NOT EXISTS idx_audit_events_type 
    ON audit_events(event_type);

CREATE INDEX IF NOT EXISTS idx_audit_events_acknowledged 
    ON audit_events(acknowledged) WHERE NOT acknowledged;

-- Add session table for authentication
CREATE TABLE IF NOT EXISTS sessions (
    id SERIAL PRIMARY KEY,
    session_token VARCHAR(64) UNIQUE NOT NULL,
    created_at TIMESTAMP DEFAULT NOW(),
    expires_at TIMESTAMP NOT NULL,
    ip_address VARCHAR(45)
);

CREATE INDEX IF NOT EXISTS idx_sessions_token 
    ON sessions(session_token);

CREATE INDEX IF NOT EXISTS idx_sessions_expires 
    ON sessions(expires_at);

COMMIT;

-- Verify migration
SELECT 'audit_events columns:' as info;
SELECT column_name, data_type 
FROM information_schema.columns 
WHERE table_name = 'audit_events'
ORDER BY ordinal_position;

SELECT 'sessions columns:' as info;
SELECT column_name, data_type 
FROM information_schema.columns 
WHERE table_name = 'sessions'
ORDER BY ordinal_position;
