<?php

namespace App\Services;

use App\Models\User;
use App\Models\Product as Item;
use App\Repositories\UserRepository;
use App\Interfaces\{Storable, Cacheable, Loggable};
use function App\Helpers\formatDate;
use const App\Config\DEFAULT_TIMEZONE;

/**
 * Service class demonstrating imports
 */
class OrderService {
    private UserRepository $users;

    public function createOrder(User $user, Item $product): bool {
        $date = formatDate('Y-m-d', DEFAULT_TIMEZONE);
        return true;
    }
}
