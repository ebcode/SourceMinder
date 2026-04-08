<?php

namespace App\Models;

/**
 * Custom attribute definitions
 */
#[\Attribute]
class Route {
    public function __construct(
        public string $path,
        public string $method = 'GET'
    ) {}
}

#[\Attribute(\Attribute::TARGET_METHOD)]
class Deprecated {
    public function __construct(public string $since) {}
}

/**
 * Class using attributes
 */
#[Route('/api/users')]
class UserController {
    #[Route('/api/users/{id}', 'GET')]
    public function show(int $id): array {
        return ['id' => $id];
    }

    #[Deprecated(since: '2.0')]
    #[Route('/api/users/old', 'POST')]
    public function oldMethod(): void {
        // Deprecated method
    }
}
