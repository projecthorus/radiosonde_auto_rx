// Scan Result Chart Setup

var scan_chart_spectra;
var scan_chart_peaks;
var scan_chart_threshold;
var scan_chart_obj;
var scan_chart_latest_timestamp;
var scan_chart_last_drawn = "none";

function setup_scan_chart(){
	scan_chart_spectra = {
	    xs: {
	        'Spectra': 'x_spectra'
	    },
	    columns: [
	        ['x_spectra',autorx_config.min_freq, autorx_config.max_freq],
	        ['Spectra',0,0]
	    ],
		colors: {
			Spectra: "#2f77b4"
		},
	    type:'line'
	};

	scan_chart_peaks = {
	    xs: {
	        'Peaks': 'x_peaks'
	    },
	    columns: [
	        ['x_peaks',0],
	        ['Peaks',0]
	    ],
		colors: {
			Peaks: "#ff7f0e"
		},
	    type:'scatter'
	};

	scan_chart_threshold = {
	    xs:{
	        'Threshold': 'x_thresh'
	    },
	    columns:[
	        ['x_thresh',autorx_config.min_freq, autorx_config.max_freq],
	        ['Threshold',autorx_config.snr_threshold,autorx_config.snr_threshold]
	    ],
		colors: {
			Threshold: "#2ca02c"
		},
	    type:'line'
	};

	scan_chart_obj = c3.generate({
	    bindto: '#scan_chart',
	    data: scan_chart_spectra,
        transition: {
            duration: 0
        },
        tooltip: {
            format: {
                title: function (d) { return (Math.round(d * 1000) / 1000) + " MHz"; },
                value: function (value) { return value + " dB"; }
            }
        },
	    axis:{
	        x:{
	            tick:{
                    values: [
                        400, 400.5, 401, 401.5, 402, 402.5, 403,
                        403.5, 404, 404.5, 405, 405.5, 406,
                        1676, 1678, 1680, 1682, 1684, 1686, 1688,
                        1690, 1692, 1694, 1696, 1698, 1700
                    ],
                    outer: false
	            },
	            label:"Frequency (MHz)"
	        },
	        y:{
	            label:"Power (dB - Uncalibrated)"
	        }
	    },
		size: {
			height: 200
		},
		legend: {
			show: false
		},
	    point:{r:10}
	});
}

function redraw_scan_chart(){
	// Plot the updated data.
	if(!scan_chart_latest_timestamp || scan_chart_last_drawn === scan_chart_latest_timestamp){
		// No need to re-draw.
		//console.log("No need to re-draw.");
		return;
	}
	scan_chart_obj.load(scan_chart_spectra);
	scan_chart_obj.load(scan_chart_peaks);
	scan_chart_obj.load(scan_chart_threshold);

	scan_chart_last_drawn = scan_chart_latest_timestamp;

	//console.log("Scan plot redraw - " + scan_chart_latest_timestamp);

	// Run dark mode check again to solve render issues.
	var z = getCookie('dark');
		if (z == 'true') {
			changeTheme(true);
		} else if (z == 'false') {
			changeTheme(false);
		} else if (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches) {
			changeTheme(true);
		} else {
			changeTheme(false);
		}

	// Show the latest scan time.
	if (getCookie('UTC') == 'false') {
		var date = new Date(scan_chart_latest_timestamp);
		var date_converted = date.toLocaleString(window.navigator.language,{hourCycle:'h23', year:"numeric", month:"2-digit", day:'2-digit', hour:'2-digit',minute:'2-digit', second:'2-digit'});
	} else {
		var date_converted = scan_chart_latest_timestamp.slice(0, 19).replace("T", " ") + ' UTC'
	}
	$('#scan_results').html('<b>Latest Scan:</b> ' + date_converted);
}
