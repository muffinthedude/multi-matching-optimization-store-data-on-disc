#!/bin/bash

DB_NAME="models_sql.db"  # Name of your SQLite database

# Create the database file (if it doesn't exist)
if [ ! -f "$DB_NAME" ]; then
    touch "$DB_NAME"
    echo "Database '$DB_NAME' created."
else
    echo "Database '$DB_NAME' already exists."
fi

# Create the table if it doesn't exist
sqlite3 "$DB_NAME" <<EOF
CREATE TABLE IF NOT EXISTS models (
    g1_id INTEGER,
    g2_id INTEGER,
    gm_model BLOB,
    PRIMARY KEY (g1_id, g2_id)
);
EOF

echo "Table 'models' created or already exists."

# Insert keys (0 to 9) for both g1_id and g2_id
for g1 in {0..9}; do
    for g2 in {0..9}; do
        sqlite3 "$DB_NAME" "INSERT INTO models (g1_id, g2_id) VALUES ($g1, $g2) ON CONFLICT DO NOTHING;"
    done
done

echo "Inserted (g1_id, g2_id) pairs from 0 to 9 into the 'models' table."