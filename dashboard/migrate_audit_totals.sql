-- C-Sentinel Dashboard Migration: Add Cumulative Audit Totals
-- Run this on your sentinel PostgreSQL database
-- 
-- Usage: psql -U sentinel -d sentinel -f migrate_audit_totals.sql

BEGIN;

-- Add cumulative audit columns to hosts table
ALTER TABLE hosts ADD COLUMN IF NOT EXISTS audit_auth_failures_total INTEGER DEFAULT 0;
ALTER TABLE hosts ADD COLUMN IF NOT EXISTS audit_sudo_count_total INTEGER DEFAULT 0;
ALTER TABLE hosts ADD COLUMN IF NOT EXISTS audit_sensitive_access_total INTEGER DEFAULT 0;
ALTER TABLE hosts ADD COLUMN IF NOT EXISTS audit_brute_force_count INTEGER DEFAULT 0;
ALTER TABLE hosts ADD COLUMN IF NOT EXISTS audit_totals_since TIMESTAMP DEFAULT NOW();

-- Add index for efficient reset queries
CREATE INDEX IF NOT EXISTS idx_hosts_audit_totals_since ON hosts(audit_totals_since);

COMMIT;

-- Verify migration
SELECT 
    column_name, 
    data_type, 
    column_default
FROM information_schema.columns 
WHERE table_name = 'hosts' 
    AND column_name LIKE 'audit_%'
ORDER BY column_name;
