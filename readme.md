# StellarStellaris 

Patching Stellaris in memory to enhance performance, especially late game. This is currently 3.0.1 Linux ONLY.

## WARNING WARNING WARNING 

This is pre-Beta software, use at your own risk. It only attaches in memory, so ultimately if it breaks something you can just restart the game, but it could corrupt a save or cause crashes. If it does, please let me know by opening a github issue.

Currently there are no binaries provided and you'll need to compile it yourself if you'd like to use it.

It may leak memory, There is no protection against installing it multiple times on 1 instance of Stellaris and doing so will leak a (tiny) amount of memory and may cause some weird minor UI issues (If you catch it in the right millisecond, you may get a GUI Element that is "closed" but still showing).

## What it does 
This app currently applies patches to a running instance of (linux) Stellaris to improve performance and fix bugs. In the future it may add modding functionality.

## How it does it? 
It uses pre-defined memory addresses and the ptrace API to patch in new code or assembly of a running process in memory. All the patched issues are found by hand by me with a dissassembler and debugger. This is fairly painstaking and slow as there is no source code, only assembly machine code.

## Building

`clang main.c -o stellarstellaris`

## Running
Start stellaris, then run the compiled executable with the pid. It will either error out (if it's unable to find the right version magic), or apply itself.

`./stellarstellaris `pgrep stellaris`

## Background / Why does it exist
I am a high level systems engineer that usually does work for large corporations on large, distributed, applications. In my past experience one of my primary specialties has been finding performance issues and bugs/design flaws in unknown codebases. Because of some of the jobs I've had, I've worked in a multitude of programming and scripting languages (C, C++, C#, Java, Visual Basic, Python, PHP, JavaScript, Assembly, Lua, perl, SAS, R, VHDL, COBOL, etc). That said, I'm not really a software developer. Most of my job experience has been in finding and fixing problems in complex systems (hundreds or thousands of servers running dozens of applications) in a high pressure environment with extremely tight timelines (IE: hours).

I also have about 2000 hours in Stellaris. I've almost always been dissapointed with late game performance, so I finally decided to apply my skillset to the game, and reached out to some of the Paradox team on LinkedIn and offered to provide my services to find and fix problems, for free, and I even said I was willing to sign a non disclosure agreement and intellectual property rights assignments for any work I did (basically, make sure their bases are covered legally). Unfortunately I got no response, so I decided to start looking into it "the hard way" by doing it without any code, via a disassembler and debugger.

This code is the result of my work, which I hope to expand in the future. I'd really like to improve the moddability of the game, ideally by adding a embedded lua interpreter with access to more of the internals of the game, but that may be a bit too lofty of a goal without access to the code. At the least I'd like to expand GUI modability, as I'd really like a megastructure GUI and more sidebar buttons for mods.

If you'd like to support my work so I can spend more time on it you can contribute to my Patreon at <link>

## Theory of operation / Detail of Fixes

### CFleetView::Update
 **first pass implemented**
The CFleetView::Update call is called every frame when the Fleet View is on screen (IE: when you've selected a ship). This is currently a bit resource intensive, although it has improved in 3.0. 

The patched version uses the new Outliner every n frames define to run the update once every x frames. This can cause some glitchiness when scrolling, but it seems to be mostly just graphical re-initializations.

In game testing: 
`effect While = { count < 20000 = { create_army = { name = "Ripley clones" owner = root.owner type = "assault_army" species = root.owner } }`


### CGuiObject::KillObject 
 **partially implemented**
 
The existing CGuiObject::KillObject function is very simple, it simply sets a memory offset (ptr+0xb0) to 0x1. Their are two spots in CGui::PerFrameUpdate and CGui::HandelInput where all gui objects (of which there are ~60,000-100,000 in a normal game) are looped through, checking for this memory flag. As both of these functions are called in every frame (although those loops may only process a portion of all GuiObjects depending on their state) this increases frame time. I've patching CGuiObject::KillObject to create a in memory list of objects-to-be-killed that can be looped through with less performance requirement. In CGui::PerFrameUpdate an iterator of std::find_if that finds the first to-be-killed object and a loop that finds the remainder are removed and replaced with a loop on the new in-memory to-be-killed list and calls the same destructor function on each (ptr+0x30). In game the info command can be used to check for CGuiObject leaks, as the gui object counter will continue incrementing if they are not being destroyed.

### Outliner Starbase Group - Gui Object creation/deletion rate 
 **not implemented, needs investigation**
 
While working on CGuiObject::KillObject I noticed that when the Starbase section of the Outliner is open GuiObjects are continuously created and deleted every frame. No other outliner section is doing this so I'd guess it's a bug. Likely doesn't cause a huge performance impact as it looks like 4 objects, but it should be investigated eventually.

### ParticleUpdate 
 **Not implemented, needs investigation**
 
There seems to be a job system intended to "steal" cpu time during particle updates and push it out to multiple threads, but it seems like this can get in a buggy state and cause issues, maybe due to the context switching and overhead, maybe due to some added locking intended to protect things. It's been hard to reproduce, but I've seen fairly extreme lag spikes start occuring in systems with heavy particle effects (gigastructure Birch world) that is immedietly eliminated when ParticleUpdate and CPdxParticleObject::RenderBuckets are patched out. There is also some frame-time jitter that is eliminated with these two, although it does seem minor.

### CCountry::ListSpecies 
 **Not Implemented, needs investigation**
 
CCountry::ListSpecies appears to be called in the frame update, in order to determine habitability for the colonizable planet icons on the map. If there are a significant number of species obviously doing this every frame is going to be performance impacting, and I can't think of any reason that information would need to be frame-accurate (If the colonizable planet icon shows up 10 seconds AFTER you get a new species, I'm sure that'd be fine, it doesn't need to be within 20 milliseconds). The parent function doees appear to have some kind of Caching in it, so I should look into that as well.

### COutliner::InternalUpdate 
 **Not Implemented, needs investigation **
 
This function is called in every frame whether all outliner sections are closed or not, and even if the entire outliner is closed. It doesn't seem to impact anything if it's patched out while the outliner is closed, need to investigating if there is anything hacked into this function that is updating other game state. Also, if it's just updating the data within the outliner then I don't think it needs to be per-frame,. if the outliner updated every second that'd be 1/60th the load and still up-to-date.

### Other Outliner 
 **Not implemented, needs investigation**
 
Each section in the outliner has a performance impact, some a LOT more than others (starbases and military fleets being the two big late game impactors, but it probably depends on the gamestate, if you have 10,000 science ships I'm sure that tab would suck). The performance impact is eliminated (aside from the above InternalUpdate) when the sections are closed, although oddly if the sections are open when the outliner itself is closed, the impact remains. I believe most of the impact is from aggregating data to build things like military power strings for fleets too frequently (once per frame), it should be simple to decrease the frequency of these updates assuming the UI isn't totally recent every frame (which it doesn't appear to be). That said, it looked like there may be an improvement to this in 3.0 so this one will wait for that.
