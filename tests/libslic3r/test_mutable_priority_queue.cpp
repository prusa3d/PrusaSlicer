#include <catch2/catch_test_macros.hpp>

#include <queue>
#include <random>
#include <algorithm>

#include "libslic3r/MutablePriorityQueue.hpp"

using namespace Slic3r;

// based on https://raw.githubusercontent.com/rollbear/prio_queue/master/self_test.cpp
// original source Copyright Björn Fahller 2015, Boost Software License, Version 1.0, http://www.boost.org/LICENSE_1_0.txt

TEST_CASE("Skip addressing", "[MutableSkipHeapPriorityQueue]") {
    using skip_addressing = SkipHeapAddressing<8>;
    SECTION("block root") {
        REQUIRE(skip_addressing::is_block_root(1));
        REQUIRE(skip_addressing::is_block_root(9));
        REQUIRE(skip_addressing::is_block_root(17));
        REQUIRE(skip_addressing::is_block_root(73));
        REQUIRE(! skip_addressing::is_block_root(2));
        REQUIRE(! skip_addressing::is_block_root(3));
        REQUIRE(! skip_addressing::is_block_root(4));
        REQUIRE(! skip_addressing::is_block_root(7));
        REQUIRE(! skip_addressing::is_block_root(31));
    }
    SECTION("block leaf") {
        REQUIRE(! skip_addressing::is_block_leaf(1));
        REQUIRE(! skip_addressing::is_block_leaf(2));
        REQUIRE(! skip_addressing::is_block_leaf(3));
        REQUIRE(skip_addressing::is_block_leaf(4));
        REQUIRE(skip_addressing::is_block_leaf(5));
        REQUIRE(skip_addressing::is_block_leaf(6));
        REQUIRE(skip_addressing::is_block_leaf(7));
        REQUIRE(skip_addressing::is_block_leaf(28));
        REQUIRE(skip_addressing::is_block_leaf(29));
        REQUIRE(skip_addressing::is_block_leaf(30));
        REQUIRE(! skip_addressing::is_block_leaf(257));
        REQUIRE(skip_addressing::is_block_leaf(255));
    }
    SECTION("Obtaining child") {
        REQUIRE(skip_addressing::child_of(1) == 2);
        REQUIRE(skip_addressing::child_of(2) == 4);
        REQUIRE(skip_addressing::child_of(3) == 6);
        REQUIRE(skip_addressing::child_of(4) == 9);
        REQUIRE(skip_addressing::child_of(31) == 249);
    }
    SECTION("Obtaining parent") {
        REQUIRE(skip_addressing::parent_of(2) == 1);
        REQUIRE(skip_addressing::parent_of(3) == 1);
        REQUIRE(skip_addressing::parent_of(6) == 3);
        REQUIRE(skip_addressing::parent_of(7) == 3);
        REQUIRE(skip_addressing::parent_of(9) == 4);
        REQUIRE(skip_addressing::parent_of(17) == 4);
        REQUIRE(skip_addressing::parent_of(33) == 5);
        REQUIRE(skip_addressing::parent_of(29) == 26);
        REQUIRE(skip_addressing::parent_of(1097) == 140);
    }
}

struct ValueIndexPair
{
    int     value;
    size_t  idx = 0;
};

template<size_t block_size = 16>
static auto make_test_priority_queue()
{
    return make_miniheap_mutable_priority_queue<ValueIndexPair, block_size, false>(
        [](ValueIndexPair &v, size_t idx){ v.idx = idx; },
        [](ValueIndexPair &l, ValueIndexPair &r){ return l.value < r.value; });
}

TEST_CASE("Mutable priority queue - basic tests", "[MutableSkipHeapPriorityQueue]") {
    SECTION("a default constructed queue is empty") {
        auto q = make_test_priority_queue();
        REQUIRE(q.empty());
        REQUIRE(q.size() == 0);
    }
    SECTION("an empty queue is not empty when one element is inserted") {
        auto q = make_test_priority_queue();
        q.push({ 1 });
        REQUIRE(!q.empty());
        REQUIRE(q.size() == 1);
    }
    SECTION("a queue with one element has it on top") {
        auto q = make_test_priority_queue();
        q.push({ 8 });
        REQUIRE(q.top().value == 8);
    }
    SECTION("a queue with one element becomes empty when popped") {
        auto q = make_test_priority_queue();
        q.push({ 9 });
        q.pop();
        REQUIRE(q.empty());
        REQUIRE(q.size() == 0);
    }
    SECTION("insert sorted stays sorted") {
        auto q = make_test_priority_queue();
        for (auto i : { 1, 2, 3, 4, 5, 6, 7, 8 })
            q.push({ i });
        REQUIRE(q.top().value == 1);
        q.pop();
        REQUIRE(q.top().value == 2);
        q.pop();
        REQUIRE(q.top().value == 3);
        q.pop();
        REQUIRE(q.top().value == 4);
        q.pop();
        REQUIRE(q.top().value == 5);
        q.pop();
        REQUIRE(q.top().value == 6);
        q.pop();
        REQUIRE(q.top().value == 7);
        q.pop();
        REQUIRE(q.top().value == 8);
        q.pop();
        REQUIRE(q.empty());
    }
    SECTION("randomly inserted elements are popped sorted") {
        auto q = make_test_priority_queue();
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(1, 100000);
        int n[36000];
        for (auto& i : n) {
            i = dist(gen);
            q.push({ i });
        }

        REQUIRE(!q.empty());
        REQUIRE(q.size() == 36000);
        std::sort(std::begin(n), std::end(n));
        for (auto i : n) {
            REQUIRE(q.top().value == i);
            q.pop();
        }
        REQUIRE(q.empty());
    }
}

TEST_CASE("Mutable priority queue - reshedule first", "[MutableSkipHeapPriorityQueue]") {
    struct MyValue {
        int    value;
        int   *ptr;
        size_t idx;
    };
    SECTION("reschedule top with highest prio leaves order unchanged") {
        auto q = make_miniheap_mutable_priority_queue<MyValue, 4, false>(
            [](MyValue& v, size_t idx) { v.idx = idx; },
            [](MyValue& l, MyValue& r) { return l.value < r.value; });

        //              0  1   2   3  4   5  6   7   8
        int nums[] = { 32, 1, 88, 16, 9, 11, 3, 22, 23 };
        for (auto &i : nums)
            q.push({ i, &i, 0U });
        REQUIRE(q.top().value == 1);
        REQUIRE(q.top().ptr == &nums[1]);
        REQUIRE(*q.top().ptr == 1);

        // Update the top element.
        q.top().value = 2;
        q.update(1);

        REQUIRE(q.top().value == 2);
        REQUIRE(q.top().ptr == &nums[1]);
        q.pop();
        REQUIRE(q.top().value == 3);
        REQUIRE(q.top().ptr == &nums[6]);
        q.pop();
        REQUIRE(q.top().value == 9);
        REQUIRE(q.top().ptr == &nums[4]);
        q.pop();
        REQUIRE(q.top().value == 11);
        REQUIRE(q.top().ptr == &nums[5]);
        q.pop();
        REQUIRE(q.top().value == 16);
        REQUIRE(q.top().ptr == &nums[3]);
        q.pop();
        REQUIRE(q.top().value == 22);
        REQUIRE(q.top().ptr == &nums[7]);
        q.pop();
        REQUIRE(q.top().value == 23);
        REQUIRE(q.top().ptr == &nums[8]);
        q.pop();
        REQUIRE(q.top().value == 32);
        REQUIRE(q.top().ptr == &nums[0]);
        q.pop();
        REQUIRE(q.top().value == 88);
        REQUIRE(q.top().ptr == &nums[2]);
        q.pop();
        REQUIRE(q.empty());
    }
    SECTION("reschedule to mid range moves element to correct place") {
        auto q = make_miniheap_mutable_priority_queue<MyValue, 4, false>(
            [](MyValue& v, size_t idx) { v.idx = idx; },
            [](MyValue& l, MyValue& r) { return l.value < r.value; });

        //              0  1   2   3  4   5  6   7   8
        int nums[] = { 32, 1, 88, 16, 9, 11, 3, 22, 23 };
        for (auto& i : nums)
            q.push({ i, &i, 0U });
        REQUIRE(q.top().value == 1);
        REQUIRE(q.top().ptr == &nums[1]);
        REQUIRE(*q.top().ptr == 1);

        // Update the top element.
        q.top().value = 12;
        q.update(1);

        REQUIRE(q.top().value == 3);
        REQUIRE(q.top().ptr == &nums[6]);
        q.pop();
        REQUIRE(q.top().value == 9);
        REQUIRE(q.top().ptr == &nums[4]);
        q.pop();
        REQUIRE(q.top().value == 11);
        REQUIRE(q.top().ptr == &nums[5]);
        q.pop();
        REQUIRE(q.top().value == 12);
        REQUIRE(q.top().ptr == &nums[1]);
        q.pop();
        REQUIRE(q.top().value == 16);
        REQUIRE(q.top().ptr == &nums[3]);
        q.pop();
        REQUIRE(q.top().value == 22);
        REQUIRE(q.top().ptr == &nums[7]);
        q.pop();
        REQUIRE(q.top().value == 23);
        REQUIRE(q.top().ptr == &nums[8]);
        q.pop();
        REQUIRE(q.top().value == 32);
        REQUIRE(q.top().ptr == &nums[0]);
        q.pop();
        REQUIRE(q.top().value == 88);
        REQUIRE(q.top().ptr == &nums[2]);
        q.pop();
        REQUIRE(q.empty());
    }
    SECTION("reschedule to last moves element to correct place", "heap")
    {
        auto q = make_miniheap_mutable_priority_queue<MyValue, 4, false>(
            [](MyValue& v, size_t idx) { v.idx = idx; },
            [](MyValue& l, MyValue& r) { return l.value < r.value; });

        //              0  1   2   3  4   5  6   7   8
        int nums[] = { 32, 1, 88, 16, 9, 11, 3, 22, 23 };
        for (auto& i : nums)
            q.push({ i, &i, 0U });
        REQUIRE(q.top().value == 1);
        REQUIRE(q.top().ptr == &nums[1]);
        REQUIRE(*q.top().ptr == 1);

        // Update the top element.
        q.top().value = 89;
        q.update(1);

        REQUIRE(q.top().value == 3);
        REQUIRE(q.top().ptr == &nums[6]);
        q.pop();
        REQUIRE(q.top().value == 9);
        REQUIRE(q.top().ptr == &nums[4]);
        q.pop();
        REQUIRE(q.top().value == 11);
        REQUIRE(q.top().ptr == &nums[5]);
        q.pop();
        REQUIRE(q.top().value == 16);
        REQUIRE(q.top().ptr == &nums[3]);
        q.pop();
        REQUIRE(q.top().value == 22);
        REQUIRE(q.top().ptr == &nums[7]);
        q.pop();
        REQUIRE(q.top().value == 23);
        REQUIRE(q.top().ptr == &nums[8]);
        q.pop();
        REQUIRE(q.top().value == 32);
        REQUIRE(q.top().ptr == &nums[0]);
        q.pop();
        REQUIRE(q.top().value == 88);
        REQUIRE(q.top().ptr == &nums[2]);
        q.pop();
        REQUIRE(q.top().value == 89);
        REQUIRE(q.top().ptr == &nums[1]);
        q.pop();
        REQUIRE(q.empty());
    }
    SECTION("reschedule top of 2 elements to last") {
        auto q = make_test_priority_queue<8>();
        q.push({ 1 });
        q.push({ 2 });
        REQUIRE(q.top().value == 1);
        // Update the top element.
        q.top().value = 3;
        q.update(1);
        REQUIRE(q.top().value == 2);
    }
    SECTION("reschedule top of 3 elements left to 2nd") {
        auto q = make_test_priority_queue<8>();
        q.push({ 1 });
        q.push({ 2 });
        q.push({ 4 });
        REQUIRE(q.top().value == 1);
        // Update the top element.
        q.top().value = 3;
        q.update(1);
        REQUIRE(q.top().value == 2);
    }
    SECTION("reschedule top of 3 elements right to 2nd") {
        auto q = make_test_priority_queue<8>();
        q.push({ 1 });
        q.push({ 4 });
        q.push({ 2 });
        REQUIRE(q.top().value == 1);
        // Update the top element.
        q.top().value = 3;
        q.update(1);
        REQUIRE(q.top().value == 2);
    }
    SECTION("reschedule top random gives same resultas pop/push") {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<unsigned> dist(1, 100000);

        auto pq = make_test_priority_queue<8>();
        std::priority_queue<int, std::vector<int>, std::greater<>> stdq;

        for (size_t outer = 0; outer < 100; ++ outer) {
            int num = gen();
            pq.push({ num });
            stdq.push({ num });
            for (size_t inner = 0; inner < 100; ++ inner) {
                int newval = gen();
                // Update the top element.
                pq.top().value = newval;
                pq.update(1);
                stdq.pop();
                stdq.push({ newval });
                auto n  = pq.top().value;
                auto sn = stdq.top();
                REQUIRE(sn == n);
            }
        }
    }
}

TEST_CASE("Mutable priority queue - first pop", "[MutableSkipHeapPriorityQueue]")
{
    struct MyValue{
        size_t id;
        float val;
    };
    static constexpr const size_t count = 50000;
    std::vector<size_t> idxs(count, {0});
    auto q = make_miniheap_mutable_priority_queue<MyValue, 16, true>(
        [&idxs](MyValue &v, size_t idx) { idxs[v.id] = idx; },
        [](MyValue &l, MyValue &r) { return l.val < r.val; });
    using QueueType = decltype(q);
    THEN("Skip queue has 0th element unused, 1st element is the top of the queue.") {
        CHECK(QueueType::address::is_padding(0));
        CHECK(!QueueType::address::is_padding(1));
    }
    q.reserve(count);
    for (size_t id = 0; id < count; ++ id)
        q.push({ id, rand() / 100.f });
    MyValue v = q.top(); // copy
    THEN("Element at the top of the queue has a valid ID.") {
        CHECK(v.id >= 0);
        CHECK(v.id < count);
    }
    THEN("Element at the top of the queue has its position stored in idxs.") {
        CHECK(idxs[v.id] == 1);
    }
    q.pop();
    THEN("Element removed from the queue has its position in idxs reset to invalid.") {
        CHECK(idxs[v.id] == q.invalid_id());
    }
    THEN("Element was removed from the queue, new top of the queue has its index set correctly.") {
        CHECK(q.top().id >= 0);
        CHECK(q.top().id < count);
        CHECK(idxs[q.top().id] == 1);
    }
}

TEST_CASE("Mutable priority queue complex", "[MutableSkipHeapPriorityQueue]")
{
    struct MyValue {
        size_t id;
        float val;
    };
    size_t               count = 5000;
    std::vector<size_t>  idxs(count, {0});
    std::vector<bool>    dels(count, false);
    auto q = make_miniheap_mutable_priority_queue<MyValue, 16, true>(
        [&](MyValue &v, size_t idx) { idxs[v.id] = idx; },
        [](MyValue &l, MyValue &r) { return l.val < r.val; });
    q.reserve(count);

    auto rand_val = [&]()->float { return (rand() % 53) / 10.f; };
    for (size_t id = 0; id < count; ++ id)
        q.push({ id, rand_val() });

    auto check = [&]()->bool{
        for (size_t i = 0; i < idxs.size(); ++i) {
            if (dels[i]) {
                if (idxs[i] != q.invalid_id())
                    return false; // ERROR 
            } else {
                size_t   qid = idxs[i];
                if (qid >= q.heap_size()) { 
                    return false; // ERROR 
                }
                MyValue &mv  = q[qid]; 
                if (mv.id != i) { 
                    return false; // ERROR 
                }
            }
        }
        return true;
    };

    CHECK(check()); // initial check

    // Generate an element ID of an elmenet, which was not yet deleted, thus it is still valid.
    auto get_valid_id = [&]()->int { 
        int id = 0;
        do {
            id = rand() % count;
        } while (dels[id]);
        return id;
    };

    // Remove first 100 elements from the queue of 5000 elements, cross-validate indices.
    // Re-enter every 20th element back to the queue.
    for (size_t i = 0; i < 100; i++) {
        MyValue v = q.top(); // copy
        q.pop();
        dels[v.id] = true;
        CHECK(check());
        if (i % 20 == 0) {
            v.val = rand_val();
            q.push(v);
            dels[v.id] = false;
            CHECK(check());
            continue;
        }
        // Remove some valid element from the queue.
        int id = get_valid_id();
        CHECK(idxs[id] != q.invalid_id());
        q.remove(idxs[id]);
        dels[id] = true;
        CHECK(check());
        // and change 5 random elements and reorder them in the queue.
        for (size_t j = 0; j < 5; j++) { 
            int id = get_valid_id();
            size_t   qid = idxs[id];
            MyValue &mv  = q[qid];
            mv.val       = rand_val();
            q.update(qid);
            CHECK(check());
        }
    }
}
