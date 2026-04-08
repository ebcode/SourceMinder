<?php

namespace App\Models;

/**
 * Trait for timestamping
 */
trait Timestampable {
    protected string $createdAt;
    protected string $updatedAt;

    public function setTimestamps(): void {
        $this->createdAt = date('Y-m-d H:i:s');
        $this->updatedAt = date('Y-m-d H:i:s');
    }
}

/**
 * Trait for soft deletes
 */
trait SoftDeletes {
    protected ?string $deletedAt = null;

    public function softDelete(): void {
        $this->deletedAt = date('Y-m-d H:i:s');
    }
}

/**
 * Class using multiple traits
 */
class Post {
    use Timestampable, SoftDeletes;

    public string $title;
    public string $content;
}

// Object instantiation and method calls
$post = new Post();
$post->setTimestamps();
$post->softDelete();
