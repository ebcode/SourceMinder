void test_function(int *patterns) {
    if (execute_proximity_to_temp_table(db, patterns, include, exclude,
                                        filters, file_filter, line_range, debug) != 0) {
        return;
    }

retry_query:
    char sql[SQL_QUERY_BUFFER];
}
