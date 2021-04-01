// Scan Result Chart Setup

var scan_chart_spectra;
var scan_chart_peaks;
var scan_chart_threshold;
var scan_chart_obj;

function setup_scan_chart(){
	scan_chart_spectra = {
	    xs: {
	        'Spectra': 'x_spectra'
	    },
	    columns: [
	        ['x_spectra',autorx_config.min_freq, autorx_config.max_freq],
	        ['Spectra',0,0]
	    ],
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
	    type:'line'
	};

	scan_chart_obj = c3.generate({
	    bindto: '#scan_chart',
	    data: scan_chart_spectra,
        tooltip: {
            format: {
                title: function (d) { return (Math.round(d * 1000) / 1000) + " MHz"; },
                value: function (value) { return value + " dB"; }
            }
        },
	    axis:{
	        x:{
	            tick:{
                    culling: {
                        max: window.innerWidth > 1100 ? 10 : 4
                    },
	                format: function (x) { return x.toFixed(3); }
	            },
	            label:"Frequency (MHz)"
	        },
	        y:{
	            label:"Power (dB - Uncalibrated)"
	        }
	    },
	    point:{r:10}
	});
}