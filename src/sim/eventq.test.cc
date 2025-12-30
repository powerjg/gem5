#include <gtest/gtest.h>

#include "sim/eventq.hh"

using namespace gem5;

class EventManagerTest : public testing::Test {
  protected:
    EventQueue *eq;
    EventManager *em;

    void SetUp() override {
        eq = new EventQueue("test_queue");
        em = new EventManager(eq);
        curEventQueue(eq);
    }

    void TearDown() override {
        delete em;
        delete eq;
        curEventQueue(nullptr);
    }
};

class TestEvent : public Event {
  public:
    int processCount = 0;
    bool *deleted = nullptr;

    TestEvent() : Event() {}
    TestEvent(bool auto_delete, bool *deleted_flag = nullptr)
        : Event(Default_Pri, auto_delete ? AutoDelete : 0), deleted(deleted_flag) {}

    ~TestEvent() {
        if (deleted) *deleted = true;
    }

    void process() override {
        processCount++;
    }

    const char *description() const override { return "TestEvent"; }
};

TEST_F(EventManagerTest, ScheduleEvent) {
    TestEvent event;
    em->schedule(&event, 10);
    EXPECT_TRUE(event.scheduled());
    EXPECT_EQ(event.when(), 10);

    eq->serviceEvents(10);
    EXPECT_FALSE(event.scheduled());
    EXPECT_EQ(event.processCount, 1);
    EXPECT_EQ(eq->getCurTick(), 10);
}

TEST_F(EventManagerTest, RescheduleEvent) {
    TestEvent event;
    em->schedule(&event, 10);
    EXPECT_TRUE(event.scheduled());
    EXPECT_EQ(event.when(), 10);

    em->reschedule(&event, 20);
    EXPECT_TRUE(event.scheduled());
    EXPECT_EQ(event.when(), 20);

    // Ensure it doesn't run at tick 10
    eq->serviceEvents(10);
    EXPECT_TRUE(event.scheduled());
    EXPECT_EQ(event.processCount, 0);

    // Ensure it runs at tick 20
    eq->serviceEvents(20);
    EXPECT_FALSE(event.scheduled());
    EXPECT_EQ(event.processCount, 1);
    EXPECT_EQ(eq->getCurTick(), 20);
}

TEST_F(EventManagerTest, DescheduleEvent) {
    TestEvent event;
    em->schedule(&event, 10);
    EXPECT_TRUE(event.scheduled());

    em->deschedule(&event);
    EXPECT_FALSE(event.scheduled());

    eq->serviceEvents(10);
    EXPECT_EQ(event.processCount, 0);
    EXPECT_EQ(eq->getCurTick(), 10);
}

TEST_F(EventManagerTest, ScheduleInPast) {
    TestEvent event;
    // Advance time to 10
    eq->setCurTick(10);

    // Scheduling at 5 should fail assertion
    // Note: EXPECT_DEATH requires that the assertion calls abort() or exit()
    EXPECT_DEATH(em->schedule(&event, 5), "");
}

TEST_F(EventManagerTest, AutoDelete) {
    bool deleted = false;
    // Dynamically allocate event
    TestEvent *event = new TestEvent(true, &deleted);

    em->schedule(event, 10);
    EXPECT_TRUE(event->scheduled());

    eq->serviceEvents(10);

    // Event should have been processed and deleted
    EXPECT_TRUE(deleted);
}
