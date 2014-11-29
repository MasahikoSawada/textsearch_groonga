SET search_path = public;

DROP SCHEMA groonga CASCADE;
DELETE FROM pg_catalog.pg_am WHERE amname = 'groonga';
