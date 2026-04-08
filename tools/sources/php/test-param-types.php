<?php
// Test PHP parameter types

interface FileFilter {
    public function count(): int;
}

function simpleType(int $x): void {}

function customType(FileFilter $filter): void {}

function arrayType(array $items): void {}

function nullableType(?string $name): void {}
