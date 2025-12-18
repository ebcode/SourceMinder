<?php
$cycles[$cycle_key - 1]['balance_adjusted'] = number_format(str_replace(',', '', $cycles[$cycle_key - 1]['balance']) + $cycles[$cycle_key - 1]['cycle_buffer'], 2);

if ($cycles[$cycle_key - 1]['balance_adjusted'] == 0.00) $cycles[$cycle_key - 1]['balance_adjusted'] = '-';
// if ($cycles[$cycle_key-1]['cycle_buffer'] == 0) $cycles[$cycle_key-1]['cycle_buffer'] = '-';
