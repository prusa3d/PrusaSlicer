
# Managing Printers and Material Profiles

The goal of this article is to serve as a reference to manage local printer and material profiles used in PrusaSlicer. This entails how to backup, restore, add, and remove any and all profiles you may have (in bulk).

## Directory Location
The directory where said profiles reside can be found in the following respective locations.

### For Windows:
  ` C:\Users\*YOUR_USERNAME*\AppData\Roaming\PrusaSlicer\ `
### For Mac OS:
  ` /Users/*YOUR_USERNAME*/Library/Application Support/PrusaSlicer/ `
### For Linux:
  ` ~/.config/PrusaSlicer/ `
  
## Options for Configuration
The respective config files can be found and managed in the following directories:

| Directory | Description |
| ------ | ----------- |
| filament   | The location for all filament config files added to PrusaSlicer |
| physical_printer | The location for all config files for network connected 3D Printers added to Prusa Slicer |
| print    | The location for all printer profiles added to PrusaSlicer |
| printer | The location for all printers added to PrusaSlicer |  

## Use Cases:
These directories allow for powerful control over the various profiles and configuration files. 

### Backup/Restore
Backups can be made of these respective directories for use in other versions of PrusaSlicer and on alternative systems via copying the respective directories to the new desired location.

If copies have been made, said copies can be pasted into their respective locations to restore the desired profiles and config files.

### Add/Remove (in bulk)
A current limitation to PrusaSlicer is the handling of profiles and config files in bulk, or rather, concurrently. By copying in or deleting multiple files into the resptive directories, an alternative to this functionality can be performed. 

### **Ensure that PrusaSlicer is restarted after any and all modifications are made to these directories to reflect the changes that are made.**
