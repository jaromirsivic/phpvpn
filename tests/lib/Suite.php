<?php

declare(strict_types=1);

/** Collects the tests declared by one file and reports their outcome. */
final class Suite
{
    /** @var array<string, callable():void> */
    private array $tests = [];

    private string $group = '';

    public function group(string $group): void
    {
        $this->group = $group;
    }

    public function test(string $name, callable $body): void
    {
        $this->tests[($this->group === '' ? '' : $this->group . ' / ') . $name] = $body;
    }

    /** @return array<string, callable():void> */
    public function all(): array
    {
        return $this->tests;
    }
}
