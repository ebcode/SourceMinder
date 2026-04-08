<?php

/**
 * PHPDoc comment for namespace
 * This tests documentation comments
 */
namespace App\Models;

use App\Interfaces\UserInterface;
use App\Traits\Timestampable;
use App\Services\AuthService as Auth;

/**
 * User repository class
 * Handles user data operations
 */
class UserRepository {
    // Properties with different visibility
    public string $tableName = 'users';
    protected int $cacheTimeout = 3600;
    private array $config;
    public static $instanceCount = 0;

    private const DEFAULT_LIMIT = 100;
    public const STATUS_ACTIVE = 'active';

    /**
     * Constructor method
     */
    public function __construct(array $config = []) {
        $this->config = $config;
        self::$instanceCount++;
    }

    /**
     * Get user by ID
     */
    public function getUserById(int $userId): ?User {
        $query = "SELECT * FROM users WHERE id = ?";
        return $this->fetchOne($query, [$userId]);
    }

    /**
     * Private helper method
     */
    private function fetchOne(string $sql, array $params): mixed {
        $statement = $this->prepare($sql);
        $statement->execute($params);
        return $statement->fetch();
    }

    /**
     * Protected method for subclasses
     */
    protected function getCacheKey(string $prefix, int $id): string {
        return sprintf("%s:%d", $prefix, $id);
    }

    /**
     * Static factory method
     */
    public static function create(array $config): self {
        return new self($config);
    }

    /**
     * Magic method examples
     */
    public function __toString(): string {
        return "UserRepository[{$this->tableName}]";
    }

    public function __get(string $name) {
        return $this->config[$name] ?? null;
    }
}

/**
 * Interface definition
 */
interface RepositoryInterface {
    public function find(int $id): ?object;
    public function save(object $entity): bool;
    public function delete(int $id): bool;
}

/**
 * Trait definition
 */
trait Cacheable {
    protected array $cache = [];

    public function getFromCache(string $key): mixed {
        return $this->cache[$key] ?? null;
    }

    public function putInCache(string $key, mixed $value): void {
        $this->cache[$key] = $value;
    }
}

/**
 * User model class using trait
 */
class User implements UserInterface {
    use Timestampable, Cacheable;

    public int $id;
    public string $name;
    public string $email;
    private ?string $passwordHash;

    public function __construct(
        int $id,
        string $name,
        string $email
    ) {
        $this->id = $id;
        $this->name = $name;
        $this->email = $email;
    }

    public function authenticate(string $password): bool {
        return password_verify($password, $this->passwordHash);
    }

    public function getDisplayName(): string {
        return $this->name;
    }
}

/**
 * Standalone function
 */
function calculateTotal(array $items, float $taxRate = 0.0): float {
    $subtotal = 0.0;
    foreach ($items as $item) {
        $subtotal += $item->price * $item->quantity;
    }
    $tax = $subtotal * $taxRate;
    return $subtotal + $tax;
}

/**
 * Function with various parameter types
 */
function processData(
    string $input,
    int $limit = 10,
    ?callable $callback = null,
    array &$output = []
): bool {
    // String literal for testing
    $message = "Processing data with limit";

    // Variable assignments
    $counter = 0;
    $results = [];

    // Control structures
    if ($counter < $limit) {
        $output[] = $input;
    }

    // Loop
    while ($counter < $limit) {
        $results[] = $counter;
        $counter++;
    }

    // Foreach loop
    foreach ($results as $key => $value) {
        echo "Item $key: $value\n";
    }

    // Method call
    $repository = UserRepository::create(['db' => 'mysql']);
    $user = $repository->getUserById(123);

    // Property access
    if ($user !== null) {
        $userName = $user->name;
        $userEmail = $user->email;
    }

    // Callback invocation
    if ($callback !== null) {
        $callback($results);
    }

    return true;
}

/**
 * Arrow function (PHP 7.4+)
 */
$multiply = fn($a, $b) => $a * $b;

/**
 * Anonymous function
 */
$filter = function(array $items, callable $predicate): array {
    $filtered = [];
    foreach ($items as $item) {
        if ($predicate($item)) {
            $filtered[] = $item;
        }
    }
    return $filtered;
};

// Match expression (PHP 8.0+)
function getStatusMessage(string $status): string {
    return match($status) {
        'active' => 'User is active',
        'inactive' => 'User is inactive',
        'pending' => 'User is pending approval',
        default => 'Unknown status',
    };
}

/**
 * Enum (PHP 8.1+)
 */
enum Status: string {
    case Active = 'active';
    case Inactive = 'inactive';
    case Pending = 'pending';
}

// Global constants
define('APP_VERSION', '1.0.0');
define('DEBUG_MODE', true);

// Global variables
$globalConfig = ['debug' => true];
$apiKey = 'test-api-key-12345';

?>
