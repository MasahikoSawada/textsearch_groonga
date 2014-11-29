-- INSTALL
SET client_min_messages = warning;
\set ECHO none
\i textsearch_groonga.sql
\set ECHO all
RESET client_min_messages;

CREATE TABLE document (id integer, title varchar(100), body text, release timestamptz);

INSERT INTO document VALUES (1, 'postgres XXX XXX',
	'PostgreSQL is the world''s most advanced open source database.',
	'2010-09-20 12:34:56');
INSERT INTO document VALUES (2, 'groonga XXX YYY',
	'groonga is an open-source fulltext search engine and column store.',
	'2010-06-01 00:00:00');

CREATE INDEX grnidx ON document USING groonga (title, body, release);

INSERT INTO document VALUES (3, 'groonga YYY YYY',
	'It lets you write high-performance applications that requires fulltext search.',
	'2010-12-01 00:00:00');
INSERT INTO document VALUES (999, 'symbols',
	E'1234567890-^\\!"#$%&''()=~|@[;:],./`{+*}<>?_',
	NULL);

--
-- low-level interface (native query)
--
SELECT groonga.command(
		'select --table t' || c.relfilenode || ' --query body:@open --sortby _key --output_columns _key,body')
  FROM pg_class c
 WHERE c.oid = 'grnidx'::regclass;

SELECT groonga.command(
		'select --table t' || c.relfilenode || ' --query title:@YYY --sortby _key --output_columns _key,title')
  FROM pg_class c
 WHERE c.oid = 'grnidx'::regclass;

SELECT groonga.command(
		'select --table t' || c.relfilenode || ' --query "(title:@XXX)+(body:@open)" --sortby _key --output_columns _key,title,body')
  FROM pg_class c
 WHERE c.oid = 'grnidx'::regclass;

--
-- high-level interface (index scan)
--
SET enable_seqscan = on;
SET enable_indexscan = off;
SET enable_bitmapscan = off;

SELECT id, groonga.score(tableoid, ctid), body
  FROM document
 WHERE body %% 'open'
 ORDER BY groonga.score(tableoid, ctid) DESC, id;

SELECT id, groonga.score(tableoid, ctid), title
  FROM document
 WHERE title %% 'YYY'
 ORDER BY groonga.score(tableoid, ctid) DESC, id;

SELECT id, groonga.score(tableoid, ctid), title, body
  FROM document
 WHERE title %% 'XXX' AND body %% 'open'
 ORDER BY groonga.score(tableoid, ctid) DESC, id;

SET enable_seqscan = off;
SET enable_indexscan = on;
SET enable_bitmapscan = off;

SELECT id, groonga.score(tableoid, ctid), body
  FROM document
 WHERE body %% 'open'
 ORDER BY groonga.score(tableoid, ctid) DESC, id;

SELECT id, groonga.score(tableoid, ctid), title
  FROM document
 WHERE title %% 'YYY'
 ORDER BY groonga.score(tableoid, ctid) DESC, id;

SELECT id, groonga.score(tableoid, ctid), title, body
  FROM document
 WHERE title %% 'XXX' AND body %% 'open'
 ORDER BY groonga.score(tableoid, ctid) DESC, id;

SELECT id, groonga.score(tableoid, ctid), title, body
  FROM document
 WHERE body @@ groonga.query('YYY OR open', 'title*10||body')
 ORDER BY groonga.score(tableoid, ctid) DESC, id;

SELECT id, groonga.score(tableoid, ctid), to_char(release, 'YYYY-MM-DD HH:MI:SS') AS release
  FROM document
 WHERE release > '2010-09-01 00:00:00'
 ORDER BY groonga.score(tableoid, ctid) DESC, id;

SET enable_seqscan = off;
SET enable_indexscan = off;
SET enable_bitmapscan = on;

SELECT id, groonga.score(tableoid, ctid), body
  FROM document
 WHERE body %% 'open'
 ORDER BY groonga.score(tableoid, ctid) DESC, id;

SELECT id, groonga.score(tableoid, ctid), title
  FROM document
 WHERE title %% 'YYY'
 ORDER BY groonga.score(tableoid, ctid) DESC, id;

SELECT id, groonga.score(tableoid, ctid), title, body
  FROM document
 WHERE title %% 'XXX' AND body %% 'open'
 ORDER BY groonga.score(tableoid, ctid) DESC, id;

/*
 * OR doesn't work well because Bitmap OR plan scans groonga index
 * multiple times and does not use groonga-native OR scans.
 */
SELECT id, groonga.score(tableoid, ctid), title, body
  FROM document
 WHERE title %% 'YYY' OR body %% 'open'
 ORDER BY groonga.score(tableoid, ctid) DESC, id;

/*
 * Scans with groonga.query can retrieve OR'ed conditions at once.
 * the query can refer not only column in the SQL statement
 * but also all indexed columns; 'title' is referred in the below.
 */
SELECT id, groonga.score(tableoid, ctid), title, body
  FROM document
 WHERE body @@ groonga.query('YYY OR open', 'title*10||body')
 ORDER BY groonga.score(tableoid, ctid) DESC, id;

RESET enable_seqscan;
RESET enable_indexscan;
RESET enable_bitmapscan;
