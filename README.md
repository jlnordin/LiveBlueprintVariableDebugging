# Live Blueprint Debugger

![Live Blueprint Debugger Screenshot](live-blueprint-debugger.png)

The Live Blueprint Debugger plugin integrates the Blueprint Debugger's variables window directly into the Level Editor's details panel. When playing or simulating in the editor, a new `Blueprint` section will appear in the details panel for any selected Actor that is an instance of a Blueprint class. All of the Blueprint variables will appear in sections labeled `Blueprint Properties - [category]`, where `[category]` is the category name from the Blueprint Editor.

The above editor screenshot shows filtering for 'enemy' variables. We can see the `Enemy Current Stats` Blueprint struct inside the `Blueprint Properties - Stats` heading, corresponding to the Blueprint variables category `Stats`.

## Features
- Live Blueprint variable data directly in the Actor details panel.
- Use the details panel filter to search for specific Blueprint variables.
- When a variable changes, it is highlighted in green for 1 second in the details panel.

## Differences from the Blueprint Editor's Blueprint Debugger
- Will not expand `UObject` references or variables.
- Maximum nested expansion depth is 5 levels.
- Variable values are updated in real time.
- Variable filtering is very fast.
- Does not support breakpoints or show call stacks.

## Settings

![Project Settings Image](project-settings.png)

The Live Blueprint Debugger settings can be found in `Edit` -> `Project Settings` -> `Plugins` -> `Live Blueprint Debugger`.

### Setting - When to Show Variables

`Only While Playing or Simulating` - Only show the Blueprint section and Blueprint debugger variables when playing or simualting in the editor. This is the default mode.

`Always` - Show the Blueprint section even when not playing or simualting. Note that live variable updates are disabled when not playing or simulating.

### Setting - Highlight Values That Have Changed
Setting this to true will show a one-second green highlight animation around a Blueprint variable in the details panel when its value changes.

## Notes

The Live Blueprint Editor will create a category for public Blueprint variables under the category `Blueprint Properties - Public`. These variables are also included in the details panel under the category `Public`. Only the `Blueprint Properties - Public` category supports live updates.