# wakeuplight
Simple multicolor (children) night/wake-up light

Problem: Kids waking up in th emiddle of the night or very early, unable or unwilling to read the clock. 

Solution: This night/wake-up light changes color to indicate whether it is either bed time or out-of bed time. To maximize reliability, there are no buttons or other features that can be interacted with (which usually are an invitation for kids to explore). 

-----------
Functional description: 
1. The device will connect to a wifi network. It will glow red when connecting. When connected, it will glow green once. 
2. The device will request current time from an NTP server. It will glow purple while requesting. When obtained it will glow green once. 
3. Depending on the current time, the current color will be set. 
   - During daytme it will be green.
   - During bedtime it will be yellow.  
   - Just before bedtime ends, it will change color to red to indicate that it is almost daytime. 

-----------
Implementation: 
- Hardware: Wemos D1 mini (ESP8266) with WS2812B shield. 
- Software: Arduino IDE compatible sketch. All common variables can be changed in the top of the sketch. (wifi credentials, times, time zone, ntp server, colors, brightness)

-----------
Issues:
- Test millis() rollover, currently it will just restart system if millis() is smaller than when obtained NTP time. 
- Time accuracy is not great. Current daily NTP sync will keep it within a minute.  
- Test daylight saving time. Currently it only implements EU dst, and is untested. 

