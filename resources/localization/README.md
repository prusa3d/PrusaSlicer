# How to create / update your own language

## 1) initialisation
open the settings.ini
for each file that can contains useful translation, create / edit a "data" line to point to the said file.
The 'input' property must be the SuperSlicer.pot path
The 'output' must be the SuperSlicer.po
The 'todo' contains the path of the po file to complete.

note that the first data line has the priority over the other ones (the first translation encountered is the one used)

In this exemple, we are going to update the spanish translation.
We are going to use the old slic3++ translation and the prusa one.
So the settings.ini contains these lines :
```
data = es/Slic3r++.po
data = es/PrusaSlicer_es.po

input = SuperSlicer.pot
todo = es/todo.po
output = es/SuperSlicer.po
```

## 2) launch the utility.
* Open a console
* cd into the localization directory,
* execute 'java -jar pomergeur.jar'
It will tell you if you made some mistakes about the paths, the number of translations reused and the number to do.

## 3) complete the translation file
Then, you have to open the es/toto.po file and complete all the translation. 
* msgid lines are the english string
* msgstr is the translated one, should be empty string ("").

Important: 
* you must write it in one line, use \n to input a line change. 
* the %1, %2, ... MUST be put also in the translation, as it's a placeholder for input numbers, if one is missed the software will crash, so be careful. '%%' means '%'.

## 4) relaunch the utility

You can copy/save the todo.po in an other file in case of.
After filling the todo file, change the settings.ini:

```
data = es/todo.po
data = es/SuperSlicer.po
data = es/Slic3r++.po
data = es/PrusaSlicer_es.po

input = SuperSlicer.pot
todo = es/todo.po
output = es/SuperSlicer.po
```

And re-launch the utility.

Repeat (if needed) until you have almost nothing left in your todo.po file (one-letter translation like "." are not copied by the utility)