// Utility Functions
// Mark Jessop 2018-06-30


// Color cycling for balloon traces and icons - Hopefully 4 colors should be enough for now!
var colour_values = ['blue','red','green','purple'];
var colour_idx = 0;

// Create a set of icons for the different colour values.
var sondeAscentIcons = {};
var sondeDescentIcons = {};

// TODO: Make these /static URLS be filled in with templates (or does it not matter?)
for (_col in colour_values){
	sondeAscentIcons[colour_values[_col]] =  L.icon({
        iconUrl: "/static/img/balloon-" + colour_values[_col] + '.png',
        iconSize: [46, 85],
        iconAnchor: [23, 76]
    });
    sondeDescentIcons[colour_values[_col]] = L.icon({
	    iconUrl: "/static/img/parachute-" + colour_values[_col] + '.png',
	    iconSize: [46, 84],
	    iconAnchor: [23, 76]
    });
}
