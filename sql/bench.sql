SET client_encoding = utf8;

CREATE TABLE words (
    word text
);

INSERT INTO words VALUES('PostgreSQL');
INSERT INTO words VALUES('カリフォルニア');
INSERT INTO words VALUES('大学バークレイ校');
INSERT INTO words VALUES('コンピュータ');
INSERT INTO words VALUES('サイエンス');
INSERT INTO words VALUES('学科');
INSERT INTO words VALUES('開発');
INSERT INTO words VALUES('POSTGRES');
INSERT INTO words VALUES('データベース');
INSERT INTO words VALUES('管理システム');
INSERT INTO words VALUES('後からいくつか');
INSERT INTO words VALUES('商用データベース');
INSERT INTO words VALUES('利用');
INSERT INTO words VALUES('多くの概念');
INSERT INTO words VALUES('先駆');

CREATE TABLE bench (
    id integer,
    sentence text
);

CREATE FUNCTION random_word() RETURNS text AS
$$
SELECT word FROM words LIMIT 1 OFFSET (
  SELECT floor(random() * n)
    FROM (SELECT count(*) AS n FROM words) AS w)
$$
LANGUAGE sql STABLE;

CREATE FUNCTION random_sentence() RETURNS text AS
$$
SELECT array_to_string(array_agg(word), 'の') || 'でした。'
  FROM (SELECT word FROM words WHERE random() > 0.5 ORDER BY random()) AS t
$$
LANGUAGE sql STABLE;

INSERT INTO bench
  SELECT i, random_sentence() FROM generate_series(1, 10000) AS s(i);
ALTER TABLE bench ADD PRIMARY KEY (id);
CREATE INDEX idx_bench ON bench USING groonga (sentence);
ANALYZE bench;

CREATE TABLE pgbench_branches (dummy integer);

\! pgbench -n contrib_regression -f data/bench.sql -T5 -c8 > /dev/null
VACUUM bench;
\! pgbench -n contrib_regression -f data/bench.sql -T5 -c8 > /dev/null
