#include <gtest/gtest.h>
#include "../../examples/companion_radio/ui-new/MenuModel.h"

TEST(MenuModel, HomeCyclesLinearlyWithWraparound) {
  MenuModel menu;
  for (uint8_t expected = 1; expected < 6; expected++) {
    menu.handle(MenuModel::Action::DOWN);
    EXPECT_EQ(expected, menu.state().selected);
  }
  menu.handle(MenuModel::Action::DOWN);
  EXPECT_EQ(0, menu.state().selected);
  menu.handle(MenuModel::Action::UP);
  EXPECT_EQ(5, menu.state().selected);
  for (int8_t expected = 4; expected >= 0; expected--) {
    menu.handle(MenuModel::Action::UP);
    EXPECT_EQ(expected, menu.state().selected);
  }
}

TEST(MenuModel, NavigatesMessageChannelsAndBack) {
  MenuModel menu;
  menu.handle(MenuModel::Action::SELECT);
  EXPECT_EQ(MenuModel::Page::MESSAGE, menu.state().page);
  menu.handle(MenuModel::Action::SELECT);
  EXPECT_EQ(MenuModel::Page::CHANNELS, menu.state().page);
  menu.handle(MenuModel::Action::DOWN, 3);
  menu.handle(MenuModel::Action::DOWN, 3);
  menu.handle(MenuModel::Action::SELECT, 3);
  EXPECT_EQ(MenuModel::Page::CHANNEL, menu.state().page);
  EXPECT_EQ(2, menu.state().context);
  EXPECT_EQ(MenuModel::Page::CHANNELS, menu.parentPage());
  menu.handle(MenuModel::Action::BACK);
  EXPECT_EQ(MenuModel::Page::CHANNELS, menu.state().page);
  EXPECT_EQ(2, menu.state().selected);
}

TEST(MenuModel, NavigatesEveryContactCategory) {
  const MenuModel::Page expected[] = {
    MenuModel::Page::CLIENTS,
    MenuModel::Page::ROOMS,
    MenuModel::Page::REPEATERS,
    MenuModel::Page::MISC
  };
  for (uint8_t category = 0; category < 4; category++) {
  MenuModel menu;
  menu.handle(MenuModel::Action::DOWN);
  menu.handle(MenuModel::Action::SELECT);
    for (uint8_t i = 0; i < category; i++) menu.handle(MenuModel::Action::DOWN);
    menu.handle(MenuModel::Action::SELECT);
    EXPECT_EQ(expected[category], menu.state().page);
  }
}

TEST(MenuModel, HomeActionClearsNavigationStack) {
  MenuModel menu;
  for (uint8_t i = 0; i < 5; i++) menu.handle(MenuModel::Action::DOWN);
  menu.handle(MenuModel::Action::SELECT);
  EXPECT_EQ(MenuModel::Page::SETTINGS, menu.state().page);
  EXPECT_GT(menu.depth(), 0);
  menu.handle(MenuModel::Action::HOME);
  EXPECT_EQ(MenuModel::Page::HOME, menu.state().page);
  EXPECT_EQ(0, menu.depth());
}

TEST(MenuModel, SettingsIncludesDateTimeEditor) {
  MenuModel menu;
  for (uint8_t i = 0; i < 5; i++) menu.handle(MenuModel::Action::DOWN);
  menu.handle(MenuModel::Action::SELECT);
  EXPECT_EQ(MenuModel::Page::SETTINGS, menu.state().page);
  menu.handle(MenuModel::Action::DOWN);
  menu.handle(MenuModel::Action::DOWN);
  menu.handle(MenuModel::Action::SELECT);
  EXPECT_EQ(MenuModel::Page::DATE_TIME, menu.state().page);
}

TEST(MenuModel, DonateIsLastSettingsItem) {
  MenuModel menu;
  for (uint8_t i = 0; i < 5; i++) menu.handle(MenuModel::Action::DOWN);
  menu.handle(MenuModel::Action::SELECT);
  EXPECT_EQ(6, MenuModel::staticItemCount(MenuModel::Page::SETTINGS));
  for (uint8_t i = 0; i < 5; i++) menu.handle(MenuModel::Action::DOWN);
  menu.handle(MenuModel::Action::SELECT);
  EXPECT_EQ(MenuModel::Page::DONATE, menu.state().page);
}

TEST(MenuModel, EmptyDynamicListDoesNotOpenDetail) {
  MenuModel menu;
  menu.handle(MenuModel::Action::SELECT);
  menu.handle(MenuModel::Action::SELECT);
  menu.handle(MenuModel::Action::SELECT, 0);
  EXPECT_EQ(MenuModel::Page::CHANNELS, menu.state().page);
}

TEST(MenuModel, LedBehaviourIsIndependentByLedAndEvent) {
  MenuModel menu;
  menu.setLedBehaviour(0, 1, MenuModel::LedBehaviour::BLINK);
  menu.setLedBehaviour(1, 1, MenuModel::LedBehaviour::TRIPLE_BLINK);
  menu.setLedBehaviour(0, 2, MenuModel::LedBehaviour::ON);
  EXPECT_EQ(MenuModel::LedBehaviour::BLINK, menu.ledBehaviour(0, 1));
  EXPECT_EQ(MenuModel::LedBehaviour::TRIPLE_BLINK, menu.ledBehaviour(1, 1));
  EXPECT_EQ(MenuModel::LedBehaviour::ON, menu.ledBehaviour(0, 2));
  EXPECT_EQ(MenuModel::LedBehaviour::OFF, menu.ledBehaviour(1, 2));
}

TEST(MenuModel, BlinkIsOneFinitePulse) {
  MenuModel::LedPattern pattern;
  MenuModel::startLedPattern(pattern, MenuModel::LedBehaviour::BLINK, 1000);
  EXPECT_TRUE(pattern.output_on);
  EXPECT_FALSE(MenuModel::updateLedPattern(pattern, 1299));
  EXPECT_TRUE(MenuModel::updateLedPattern(pattern, 1300));
  EXPECT_FALSE(pattern.output_on);
  EXPECT_EQ(0, pattern.transitions_remaining);
}

TEST(MenuModel, FlickerProducesThreeRapidPulses) {
  MenuModel::LedPattern pattern;
  MenuModel::startLedPattern(pattern, MenuModel::LedBehaviour::FLICKER, 0);
  EXPECT_TRUE(pattern.output_on);
  for (uint32_t now = 60; now <= 300; now += 60) {
    EXPECT_TRUE(MenuModel::updateLedPattern(pattern, now));
  }
  EXPECT_FALSE(pattern.output_on);
  EXPECT_EQ(0, pattern.transitions_remaining);
}

TEST(MenuModel, OnAndOffAreSteady) {
  MenuModel::LedPattern pattern;
  MenuModel::startLedPattern(pattern, MenuModel::LedBehaviour::ON, 0);
  EXPECT_TRUE(pattern.output_on);
  EXPECT_FALSE(MenuModel::updateLedPattern(pattern, 10000));

  MenuModel::startLedPattern(pattern, MenuModel::LedBehaviour::OFF, 10000);
  EXPECT_FALSE(pattern.output_on);
  EXPECT_FALSE(MenuModel::updateLedPattern(pattern, 20000));
}

TEST(MenuModel, ListNavigationWraps) {
  MenuModel menu;
  menu.handle(MenuModel::Action::SELECT);
  menu.handle(MenuModel::Action::UP);
  EXPECT_EQ(1, menu.state().selected);
  menu.handle(MenuModel::Action::DOWN);
  EXPECT_EQ(0, menu.state().selected);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
