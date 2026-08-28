-- Growing altitude, as printed on the bag. TEXT on purpose: bags say
-- "1300m", "1200–1900 m", "up to 2100 masl" — a number column would force
-- lossy parsing of what is really a label.
ALTER TABLE bean ADD COLUMN altitude TEXT;
