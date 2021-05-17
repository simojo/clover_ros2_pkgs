#include <chrono>
#include <memory>
// #include <boost/algorithm/string.hpp>

#include "rclcpp/rclcpp.hpp"
#include "clover_ros2/srv/set_led_effect.hpp"
#include "led_msgs/srv/set_le_ds.hpp"
#include "led_msgs/msg/led_state.hpp"
#include "led_msgs/msg/led_state_array.hpp"

// #include "sensor_msgs/msg/battery_state.h"
//#include "mavros_msgs/msg/State.h"

using namespace std::chrono_literals;

class CloverLEDController : public rclcpp::Node
{
    public:
        CloverLEDController();
        void callSetLeds();
        void rainbow(uint8_t n, uint8_t& r, uint8_t& g, uint8_t& b);
        void fill(uint8_t r, uint8_t g, uint8_t b);
        // void proceed(const ros::TimerEvent& event);
        bool setEffect(std::shared_ptr<clover_ros2::srv::SetLEDEffect::Request> req, std::shared_ptr<clover_ros2::srv::SetLEDEffect::Response> res);
        void handleState(const led_msgs::msg::LEDStateArray::SharedPtr msg);
        // void notify(const std::string& event);
        // void handleMavrosState(const mavros_msgs::State& msg);

    private:
        std::shared_ptr<clover_ros2::srv::SetLEDEffect::Request> current_effect;
        int led_count;
        // ros::Timer timer;
        // ros::Time start_time;
        std::chrono::seconds blink_rate, blink_fast_rate, flash_delay, fade_period, wipe_period, rainbow_period;
        double low_battery_threshold;
        bool blink_state;
        std::shared_ptr<led_msgs::srv::SetLEDs::Request> set_leds;
        std::shared_ptr<led_msgs::msg::LEDStateArray> state, start_state;

        rclcpp::Client<led_msgs::srv::SetLEDs>::SharedPtr set_leds_srv;
        rclcpp::Subscription<led_msgs::msg::LEDStateArray>::SharedPtr state_sub;
        rclcpp::Service<clover_ros2::srv::SetLEDEffect>::SharedPtr set_effect;

        //mavros_msgs::State mavros_state;
        int counter;
};

CloverLEDController::CloverLEDController() : Node("led")
{
    double blink_rate, blink_fast_rate, flash_delay, fade_period, wipe_period, rainbow_period;
    this->get_parameter_or("blink_rate",blink_rate, 2.0);
	this->get_parameter_or("blink_fast_rate",blink_fast_rate, blink_rate * 2);
	this->get_parameter_or("fade_period",fade_period, 0.5);
	this->get_parameter_or("wipe_period",wipe_period, 0.5);
	this->get_parameter_or("flash_delay",flash_delay, 0.1);
	this->get_parameter_or("rainbow_period",rainbow_period, 5.0);
	this->get_parameter_or("notify/low_battery/threshold", this->low_battery_threshold, 3.7);

    this->blink_rate =      std::chrono::nanoseconds(blink_rate * 1000);
    this->blink_fast_rate = std::chrono::nanoseconds(blink_fast_rate * 1000);
    this->fade_period =     std::chrono::nanoseconds(fade_period * 1000);
    this->wipe_period =     std::chrono::nanoseconds(wipe_period * 1000);
    this->flash_delay =     std::chrono::nanoseconds(flash_delay * 1000);
    this->rainbow_period =  std::chrono::nanoseconds(rainbow_period * 1000);
    
    // First need to wait for service
    // ros::service::waitForService("set_leds"); // cannot work without set_leds service
    this->set_leds_srv = this->create_client<led_msgs::srv::SetLEDs>("set_leds");
    this->set_leds_srv->wait_for_service(1s);

    this->state_sub = this->create_subscription<led_msgs::msg::LEDStateArray>(
        "State", 10,
        std::bind(&CloverLEDController::handleState, this, std::placeholders::_1)
    );
    this->set_effect = this->create_service<clover_ros2::srv::SetLEDEffect>(
        "set_effect",
        std::bind(&CloverLEDController::setEffect, this, std::placeholders::_1, std::placeholders::_2)
    );
}

void CloverLEDController::callSetLeds()
{
	auto res = this->set_leds_srv->async_send_request(this->set_leds);
    if (res.wait_for(15s) != std::future_status::ready)
    {
		RCLCPP_WARN(this->get_logger(), "Calling set_leds failed");//, this->set_leds->message.c_str());
	} else {
        RCLCPP_INFO(this->get_logger(), "Succesfully called leds");
    }
}

void CloverLEDController::rainbow(uint8_t n, uint8_t& r, uint8_t& g, uint8_t& b)
{
	if (n < 255 / 3) {
		r = n * 3;
		g = 255 - n * 3;
		b = 0;
	} else if (n < 255 / 3 * 2) {
		n -= 255 / 3;
		r = 255 - n * 3;
		g = 0;
		b = n * 3;
	} else {
		n -= 255 / 3 * 2;
		r = 0;
		g = n * 3;
		b = 255 - n * 3;
	}
}

void CloverLEDController::fill(uint8_t r, uint8_t g, uint8_t b)
{
	this->set_leds->leds.resize(this->led_count);
	for (int i = 0; i < led_count; i++) {
		this->set_leds->leds[i].index = i;
		this->set_leds->leds[i].r = r;
		this->set_leds->leds[i].g = g;
		this->set_leds->leds[i].b = b;
	}
	this->callSetLeds();
}

void CloverLEDController::handleState(const led_msgs::msg::LEDStateArray::SharedPtr msg)
{
    this->state = msg;
    this->led_count = this->state->leds.size();
}

bool CloverLEDController::setEffect(std::shared_ptr<clover_ros2::srv::SetLEDEffect::Request> req, std::shared_ptr<clover_ros2::srv::SetLEDEffect::Response> res)
{
    res->success = true;

	if (req->effect == "") {
		req->effect = "fill";
	}

	if (req->effect != "flash" && req->effect != "fill" && this->current_effect->effect == req->effect &&
	    this->current_effect->r == req->r && this->current_effect->g == req->g && this->current_effect->b == req->b) {
		res->message = "Effect already set, skip";
		return true;
	}

	if (req->effect == "fill") {
		this->fill(req->r, req->g, req->b);

	} else if (req->effect == "blink") {
		// timer.setPeriod(ros::Duration(1 / blink_rate), true);
		// timer.start();

	} else if (req->effect == "blink_fast") {
		// timer.setPeriod(ros::Duration(1 / blink_fast_rate), true);
		// timer.start();

	} else if (req->effect == "fade") {
		// timer.setPeriod(ros::Duration(0.05), true);
		// timer.start();

	} else if (req->effect == "wipe") {
		// timer.setPeriod(ros::Duration(wipe_period / led_count), true);
		// timer.start();

	} else if (req->effect == "flash") {
		// rclcpp::Duration delay(this->flash_delay);
		this->fill(0, 0, 0);
        rclcpp::sleep_for(this->flash_delay);
		// delay.sleep();
		this->fill(req->r, req->g, req->b);
        rclcpp::sleep_for(this->flash_delay);
		// delay.sleep();
		this->fill(0, 0, 0);
        rclcpp::sleep_for(this->flash_delay);
		// delay.sleep();
		// this->fill(req->r, req->g, req->b);
        rclcpp::sleep_for(this->flash_delay);
		// delay.sleep();
		this->fill(0, 0, 0);
        rclcpp::sleep_for(this->flash_delay);
		// delay.sleep();
		if (this->current_effect->effect == "fill"||
		    this->current_effect->effect == "fade" ||
		    this->current_effect->effect == "wipe") {
			// restore previous filling
			for (int i = 0; i < led_count; i++) {
				this->fill(this->current_effect->r, this->current_effect->g, this->current_effect->b);
			}
			callSetLeds();
		}
		return true; // this effect happens only once

	} else if (req->effect == "rainbow_fill") {
		// timer.setPeriod(ros::Duration(rainbow_period / 255), true);
		// timer.start();

	} else if (req->effect == "rainbow") {
		// timer.setPeriod(ros::Duration(rainbow_period / 255), true);
		// timer.start();

	} else {
		res->message = "Unknown effect: " + req->effect + ". Available effects are fill, fade, wipe, blink, blink_fast, flash, rainbow, rainbow_fill.";
		RCLCPP_ERROR(this->get_logger(), "%s", res->message.c_str());
		res->success = false;
		return false;
	}

	// set current effect
	this->current_effect = req;
	this->counter = 0;
	this->start_state = this->state;
	// this->start_time = ros::Time::now();

	return true;
}

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<CloverLEDController>());
    rclcpp::shutdown();
    return 0;
}