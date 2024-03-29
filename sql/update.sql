CREATE TABLE item (name text, counter integer) WITH (fillfactor = 50);
INSERT INTO item VALUES ('foo', 0);
INSERT INTO item SELECT 'bar', 0 FROM generate_series(1, 999);
CREATE INDEX item_idx ON item USING groonga (name);
ANALYZE item;

SET enable_seqscan = off;
SELECT * FROM item WHERE name %% 'foo';
UPDATE item SET counter = counter + 1;
SELECT * FROM item WHERE name %% 'foo';
UPDATE item SET counter = counter + 1;
SELECT * FROM item WHERE name %% 'foo';
UPDATE item SET counter = counter + 1;
SELECT * FROM item WHERE name %% 'foo';
UPDATE item SET counter = counter + 1;
SELECT * FROM item WHERE name %% 'foo';
VACUUM item;
SELECT * FROM item WHERE name %% 'foo';
UPDATE item SET counter = counter + 1;
SELECT * FROM item WHERE name %% 'foo';
VACUUM item;
SELECT * FROM item WHERE name %% 'foo';
UPDATE item SET counter = counter + 1;
SELECT * FROM item WHERE name %% 'foo';
UPDATE item SET counter = counter + 1;
SELECT * FROM item WHERE name %% 'foo';
RESET enable_seqscan;
