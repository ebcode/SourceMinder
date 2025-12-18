void test() {
    if (execute_proximity_to_temp_table(db, patterns, include, exclude,
                                        filters, file_filter, line_range, debug) != 0) {
        return;
    }
}
