# OpenLCC RP2040 Firmware for Lelit Bianca

This is an evolution of [magnusnordlander/smart-lcc](https://github.com/magnusnordlander/smart-lcc), which in turn is based on the protocol dissection I did in [magnusnordlander/lelit-bianca-protocol](https://github.com/magnusnordlander/lelit-bianca-protocol).

## Compatibility

This firmware is compatible with Open LCC Board R1A through R2B. Furthermore, it's *known* to be compatible with the Bianca V2, but it's strongly suspected that it is compatible with Bianca V1, and Bianca V3 (with exception for the
 new Power LED). If you have a Bianca V3 and is interested in installing this project, let me (@magnusnordlander) know and we can work together on getting it fully compatible.

## Disclaimer

Considering this plugs in to an expensive machine it bears to mention: Anything you do with this, you do at your own risk. Components have been fried already during the course of this project. Your machine uses both line voltage power, high pressured hot water, steam and other dangerous components. There is a risk of both damaging the machine, personal injury and property damage, the liability for which you assume yourself. This is not the stage to get on board with this project if you aren't willing to deal with those risks.

## Status

Consider this project beta quality.

### A note on Bianca versions

There are (at the time of writing) three versions of the Bianca, V1, V2, and V3. To my understanding it should work on a Bianca V1, but it's untested. As for the Bianca V3, it features upgraded hardware. Lelit sells an upgrade kit to upgrade a V1 or V2. The differences are as follows:

* A new solenoid to control full/low flow from the pump (part of the upgrade kit)
  * Has the same P/N as the V2 one, but at least in my machine the actual parts were different.
* An LCC with new firmware (part of the upgrade kit)
  * Available as either P/N 9600147, or 9600124. V2 one was 9600045.
* The power light now an LED and software controlled (not part of the upgrade kit)
  * Available as P/N 3000056, but it's ridiculously expensive

It also uses a different Gicar Control Box, but since the V3 upgrade kit doesn't include it, my suspicion is that the changes in it are marginal (it could be as simple as the box having a different sticker). The new part number is 9600125, and the old one was 9600046.

I have done some reverse engineering of the Control Board, and as such I have gained a better understanding of how it works, and how the protocol works. Interestingly, new solenoid is just a regular solenoid, so my suspicion is that the LCC basically PWMs the solenoid to create the low flow mode. I would love a protocol dump between a V3 LCC and the Control Board to confirm though. I would also love photos of the Control Board internals, just to confirm that there are no relevant differences.

### Versioning
This project uses Semver. The major version number is increased whe RP2040 <-> ESP32 protocol version is increased (as that is a BC break).

## Project goals

Create a firmware for using the Open LCC in a Lelit Bianca to its fullest extent.

## Architecture

#### RP2040 Core 0
* System controller
    * Safety critical, uses the entire core for itself
    * Communicates with the Control Board
    * Performs a safety check, ensuring that temperatures in the boilers never exceed safe limits, and that both boilers are never running simultaneously.
    * Responsible for PID, keeping water in the boiler, running pumps etc.

#### RP2040 Core 1
* Communication with the ESP32-S3

#### Core communication protocol

##### Core1 to Core0

* One message type
    * Timestamp (absolute_time_t)
    * Brew temperature
    * Brew set point
    * Brew PID settings
    * Brew PID parameters
    * Service temperature
    * Service set point
    * Service PID settings
    * Service PID parameters
    * Eco mode
    * System state
        * Idle
        * Heatup
        * Warm
        * Sleeping
        * Bailed
        * First run (fill all the boilers before heating them)
    * Bail reason
    * Brewing
    * Filling service boiler
    * Water low

##### Core0 to Core1

* Multiple message types
    * Set Brew Set Point (Non compensated)
    * Set Brew PID Parameters
    * Set Service Set Point
    * Set Service PID Parameters
    * Set Eco Mode (on/off)
    * Set Sleep mode (on/off)
    * Unbail
    * Trigger first run
* Payloads
    * Float 1
        * Brew set point
        * Service boiler set point
        * Kp
    * Float 2
        * Ki
    * Float 3
        * Kd
    * Bool
        * Eco mode
        * Sleep mode


### Reference material
(In no particular order)

* https://www.youtube.com/watch?v=fAEDHOUJCEo
* Random facts about the stock implementation:
    * Some implementation details are available in https://www.1st-line.com/wp-content/uploads/2018/12/Parameter-settings-technical-menu-Bianca-PL162T.pdf
        * Original parameters (not necessarily applicable since PID parameters are hightly implementation dependent):
            * Brew boiler
                * Kp: 0.8
                * Ki: 0.04
                * Kd: 12
                * On/off delta: 15°C
            * Service boiler
                * Kp: 20
                * Ki: 0
                * Kd: 20
                * On/off delta: 0
            * Brew boiler offset: 10°C
        * A "full" heatup cycle goes to 130°C first, then drops to set temperature
            * Triggered when the temperature on power on is less than 70°C
        * Service boiler is not PID controlled (even though it can be). Even if it *were*, having a Ki of 0, means it's a PD controller. It's a simple on/off controller.
    * Minimum on/off-time seems to be around 100-120 ms
    * When in eco mode, a warmup consists of running the coffee boiler full blast, holding the temperature around 130°C for 4 minutes, then dropping to the set point
    * When in regular mode, there is a kind of time slot system for which boiler is used. The time slots are 1 second wide.
    * Shot saving seems to be implemented by simply blocking new brews from starting when the tank is empty. There's no fanciness with allowing it to run for a certain amount of time or anything.
    * It takes about 3 seconds for the LCC to pick up on an empty water tank (presumably to debounce the signal)

## Licensing

The firmware is MIT licensed (excepting dependencies, which have their own, compatible licenses).
