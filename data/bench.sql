\setrandom id 1 10000
BEGIN;
SELECT * FROM bench WHERE sentence %% random_word();
UPDATE bench SET sentence = random_sentence() WHERE id = :id;
END;
