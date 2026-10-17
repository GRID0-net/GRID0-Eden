<!--
# SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
-->

# GRID0-Eden

because we needed a third switch emu for the people!

This is basically just eden but modified to have the GRID0+ patches, plus networking and friend management. GRID0+ is our replacement online service for Splatoon 3, and this brings it to another emulator! you can grab a build from the releases tab, or [build it yourself](docs/Build.md).

## why a third emu?

for some people citron-neo doesnt work all that great, and I (lizzy speaking hi) have been playing around with making more eden based citrosis builds. this isnt that, but it is more functional and optimized than citron-neo!

still, the pick is up to you, as all 3 work well. this is here by popular demand, and having another option is nice. use whichever one works best on your setup!

## getting online

open **GRID0+ → Account and server…** and enter your GRID0+ account on the Network page. pick your active network adapter and turn off **Enable Airplane Mode** so the game can connect.

eden will check for GRID0+ patch updates and ask before installing them. the friends menu is under **GRID0+** too, so you can manage friends and see who's around from there.


## credits and licensing

big thanks to the [Eden](https://git.eden-emu.dev/eden-emu/eden) developers, the original yuzu contributors, and our work on [GRID0-citron](https://github.com/GRID0-net/GRID0-citron). this fork builds upon that, and the original credits and license notices stay with it.

GRID0-Eden is licensed under **GPLv3 or later**, just like Eden. see [LICENSE.txt](LICENSE.txt) for the full license. the GRID0-citron code keeps its GPLv2-or-later notices, and third-party components keep their own licenses, included in [LICENSES](LICENSES) and their source files.
