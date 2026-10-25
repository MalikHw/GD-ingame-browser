# Browser

A tabbed, in-game browser you can pop open over Geometry Dash.
btw It's a real Microsoft Edge WebView2 web page, composited into an ImGui window.

### Usage

- **F6** (Bindable): show/hide the browser.
- **Ctrl+Tab**: return keyboard focus to the browser's UI (tab bar / URL box) if you've clicked into a page and want to type into GD/ImGui again.
- Click **+** to open a new tab, click a tab's **x** to close it.
- Type a URL or a search term into the address bar and press Enter.
- Links that opens a new tab opens in a new tab. *(the floor is floor ahh)*
- The Geode settings menu provides confirmation buttons for clearing cache and cookies/site data.

### Requirements

Needs the Microsoft Edge WebView2 Runtime, which almost all Windows 10/11 installs already have. If it's missing, It tells you and guides you to install it.

### Notes

- Windows only.
- Each tab gets its own page/session but they share one browser profile.
- Linux (Even installing WebView2 in Wine/Proton) won't work. It is a problem with WebView that i could not work around...
