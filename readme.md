115

A really crappy backup tool that you shouldn't use. Cause it's not tested. And will probably delete all your data.

Seriously, if you use it, you take all responsibilities for it. That said, it's not free. 
You can use it for your own purposes but you may not redistribute it either for free or 
as part of another package, or any combination your twisted little mind conceives.

Just contact me and ask first. ;)

Note: this is a Windows 64-bit app only. It will not work on 32-bit, and I don't plan to port it. It's tested on 7, 10 and 11. Binary is in the dist folder but buyer beware! Because a misconfiguration could cause you to lose files, I'm likely to be paranoid and not offer much support, instead saying "use something else". ;)

Now supports filename munging if the source is detected as case-sensitive. This will be really annoying if a restore is needed because pretty much every single file will get the name munged. As I haven't had to do a restore yet, I have not made a tool to do it, but it's pretty straightforward. We could do a simple recursive rename tool in python or C that would be easy to port crossplatform for the fixing. Hell, bash can probably do it. All that is needed is to scan the restored folder, for every file (FILE, not FOLDER), check for '^^' and replace it with '^', otherwise remove all '^' characters.