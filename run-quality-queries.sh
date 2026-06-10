#!/bin/bash

# Script to run SQL queries from a =========-separated query file
# Usage: ./run-quality-queries.sh [query-file]   (default: code-quality-queries.txt)
DB_FILE="code-index.db"
QUERY_FILE="${1:-code-quality-queries.txt}"

if [ ! -f "$DB_FILE" ]; then
    echo "Error: $DB_FILE not found."
    exit 1
fi

# Split by =========
# Read the file and pass cleaned queries to sqlite3
# Using perl to split and handle quotes properly
perl -ne '
    BEGIN { undef $/; }
    @queries = split(/=========/, $_);
    for $q (@queries) {
        $q =~ s/^\s+|\s+$//g;
        next if !$q;
        print "--- Running Query ---\n$q\n--- Result ---\n";
        open(SQL, "|-", "sqlite3", "'"$DB_FILE"'");
        print SQL $q;
        close(SQL);
        print "\n---------------------\n";
    }
' "$QUERY_FILE"
