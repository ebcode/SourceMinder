<?php

// Test property access assignment
$wp_query->is_404 = false;

// Test reading property
$value = $wp_query->is_404;

// Test method call
$wp_query->get_posts();
