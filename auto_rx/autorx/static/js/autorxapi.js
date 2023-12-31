// auto_rx API Helpers

function update_task_list(){
    // Grab the latest task list.
    $.getJSON("get_task_list", function(data){
        var task_info = "";

        $('#stop-frequency-select').children().remove();

        added_decoders = false;

        for (_task in data){
            // Append the current task to the task list.
            if(_task.includes("SPY")){
                task_detail = _task + " - "
            }else{
                task_detail = "SDR:" + _task + " - "
            }


            if(data[_task]["freq"] > 0.0){
                $('#stop-frequency-select')
                    .append($("<option></option>")
                    .attr("value",data[_task]["freq"])
                    .text( (parseFloat( data[_task]["freq"] )/1e6).toFixed(3)));

                added_decoders = true;

                task_detail += (parseFloat( data[_task]["freq"] )/1e6).toFixed(3);

                if (data[_task].hasOwnProperty("type")){
                    task_detail += " " + data[_task]["type"];
                }
                
            } else {
                if(data[_task]["task"] == "Scanning"){
                    task_detail += "Scan";
                } else {
                    task_detail += "Idle";
                }
            }

            task_info += "<div class='sdrinfo-element'>" + task_detail + "</div>"
        }

        if(added_decoders == false){
            $('#stop-frequency-select')
                    .append($("<option></option>")
                    .attr("value","0")
                    .text("No Decoders"));
        }
        
        // Update page with latest task.
        $('#task_status').html(task_info);
        
        setTimeout(resume_web_controls,2000);
    });
}


function disable_web_controls(){
    $("#verify-password").prop('disabled', true);
    $("#start-decoder").prop('disabled', true);
    $("#stop-decoder").prop('disabled', true);
    $("#stop-decoder-lockout").prop('disabled', true);
    $("#enable-scanner").prop('disabled', true);
    $("#disable-scanner").prop('disabled', true);
    $("#frequency-input").prop('disabled', true);
    $("#sonde-type-select").prop('disabled', true);
    $("#stop-frequency-select").prop('disabled', true);   
    $("#open-controls").prop('disabled', true);
    $("#open-controls").text("DISABLED");
}

function pause_web_controls() {
    $("#verify-password").prop('disabled', true);
    $("#start-decoder").prop('disabled', true);
    $("#stop-decoder").prop('disabled', true);
    $("#stop-decoder-lockout").prop('disabled', true);
    $("#enable-scanner").prop('disabled', true);
    $("#disable-scanner").prop('disabled', true);
    $("#frequency-input").prop('disabled', true);
    $("#sonde-type-select").prop('disabled', true);
    $("#stop-frequency-select").prop('disabled', true);   
}

function resume_web_controls() {
    $("#verify-password").prop('disabled', false);
    $("#start-decoder").prop('disabled', false);
    $("#stop-decoder").prop('disabled', false);
    $("#stop-decoder-lockout").prop('disabled', false);
    $("#enable-scanner").prop('disabled', false);
    $("#disable-scanner").prop('disabled', false);
    $("#frequency-input").prop('disabled', false);
    $("#sonde-type-select").prop('disabled', false);
    $("#stop-frequency-select").prop('disabled', false);   
}


function verify_password(){
    // Attempt to verify a password provided by a user, and update the password verify indication if its ok.

    if(autorx_config["web_control"] == false){
        alert("Web Control not enabled!");
        $("#password-header").html("<h2>Web Control Disabled</h2>");
    }
    
    // Grab the password
    if ($('#password-input').val() == "") {
        if (getCookie("password") === null) {
            _api_password = "";
        } else {
            _api_password = getCookie("password");
        }
    } else {
        _api_password = $('#password-input').val();
    }
    
    setCookie("password", _api_password);

    // Do the request
    $.post(
        "check_password", 
        {"password": _api_password},
        function(data){
            // If OK, update the header to indicate the password was OK.
            $("#password-header").html("<h2>Password OK!</h2>");
            $("#password-field").hide().css("visibility", "hidden");
            $("#controls").show().css("visibility", "visible");
        }
    ).fail(function(xhr, status, error){
        // Otherwise, we probably got a 403 error (forbidden) which indicates the password was bad.
        if(error == "FORBIDDEN"){
            $("#password-header").html("<h2>Incorrect Password</h2>");
            //alert("Incorrect Password!");
        }
    });
}

function disable_scanner(){
    // Disable the scanner.

    // Re-verify the password. This will occur async, so wont stop the main request from going ahead,
    // but will at least present an error for the user.
    verify_password();        

    // Grab the password
    _api_password = getCookie("password");

    // Do the request
    $.post(
        "disable_scanner", 
        {"password": _api_password},
        function(data){
            //console.log(data);
            // Need to figure out where to put this data..
            //alert("Scanner disable request received - please wait until SDR is shown as Not Tasked before issuing further requests.")
            pause_web_controls();
            setTimeout(resume_web_controls,10000);
            
        }
    ).fail(function(xhr, status, error){
        console.log(error);
        // Otherwise, we probably got a 403 error (forbidden) which indicates the password was bad.
        if(error == "FORBIDDEN"){
            $("#password-header").html("<h2>Incorrect Password</h2>");
        } else if (error == "NOT FOUND"){
            // Scanner isn't running. Don't do anything.
            alert("Scanner not running!")
        } else if (error == "INTERNAL SERVER ERROR"){
            // Scanner might not have started up yet.
            alert("Scanner not initialised... (try again!)");
        }
    });
}

function enable_scanner(){
    // Enable the scanner.

    // Re-verify the password. This will occur async, so wont stop the main request from going ahead,
    // but will at least present an error for the user.
    verify_password();

    // Grab the password
    _api_password = getCookie("password");

    // Do the request
    $.post(
        "enable_scanner", 
        {"password": _api_password},
        function(data){
            //console.log(data);
            pause_web_controls();
            setTimeout(resume_web_controls,10000);
            // Need to figure out where to put this data..
        }
    ).fail(function(xhr, status, error){
        console.log(error);
        // Otherwise, we probably got a 403 error (forbidden) which indicates the password was bad.
        if(error == "FORBIDDEN"){
            $("#password-header").html("<h2>Incorrect Password</h2>");
        }
    });
}

function stop_decoder(){
    // Stop the decoder on the requested frequency

    // Re-verify the password. This will occur async, so wont stop the main request from going ahead,
    // but will at least present an error for the user.
    verify_password();

    // Grab the password
    _api_password = getCookie("password");

    // Grab the selected frequency
    _decoder = $('#stop-frequency-select').val();

    // Do the request
    $.post(
        "stop_decoder", 
        {password: _api_password, freq: _decoder},
        function(data){
            //console.log(data);
            pause_web_controls();
            setTimeout(resume_web_controls,10000);
            // Need to figure out where to put this data..
        }
    ).fail(function(xhr, status, error){
        console.log(error);
        // Otherwise, we probably got a 403 error (forbidden) which indicates the password was bad.
        if(error == "FORBIDDEN"){
            $("#password-header").html("<h2>Incorrect Password</h2>");
        } else if (error == "NOT FOUND"){
            // Scanner isn't running. Don't do anything.
            alert("Decoder on supplied frequency not running!");
        }
    });
}

function stop_decoder_lockout(){
    // Stop the decoder on the requested frequency, and lockout frequency

    // Re-verify the password. This will occur async, so wont stop the main request from going ahead,
    // but will at least present an error for the user.
    verify_password();

    // Grab the password
    _api_password = getCookie("password");

    // Grab the selected frequency
    _decoder = $('#stop-frequency-select').val();

    // Do the request
    $.post(
        "stop_decoder", 
        {password: _api_password, freq: _decoder, lockout: 1},
        function(data){
            //console.log(data);
            pause_web_controls();
            setTimeout(resume_web_controls,10000);
            // Need to figure out where to put this data..
        }
    ).fail(function(xhr, status, error){
        console.log(error);
        // Otherwise, we probably got a 403 error (forbidden) which indicates the password was bad.
        if(error == "FORBIDDEN"){
            $("#password-header").html("<h2>Incorrect Password</h2>");
        } else if (error == "NOT FOUND"){
            // Scanner isn't running. Don't do anything.
            alert("Decoder on supplied frequency not running!");
        }
    });
}

function start_decoder(){
    // Start a decoder on the requested frequency

    // Re-verify the password. This will occur async, so wont stop the main request from going ahead,
    // but will at least present an error for the user.
    verify_password();

    // Grab the password
    _api_password = getCookie("password");

    // Grab the selected frequency
    _freq = $('#frequency-input').val();

    // Grab the selected type
    _type = $('#sonde-type-select').val();

    // Parse to a floar.
    _freq_float = parseFloat(_freq);
    if(_freq_float > autorx_config["max_freq"]){
        alert("Supplied frequency above maximum (" + autorx_config["max_freq"] + " MHz)");
        return;
    }
    if(_freq_float < autorx_config["min_freq"]){
        alert("Supplied frequency below minimum (" + autorx_config["min_freq"] + " MHz)");
        return;
    }

    _freq_hz = (_freq_float*1e6).toFixed(1);

    // Do the request
    $.post(
        "start_decoder", 
        {password: _api_password, freq: _freq_hz, type: _type},
        function(data){
            alert("Added requested decoder to results queue.")
            pause_web_controls();
            setTimeout(resume_web_controls,10000);
        }
    ).fail(function(xhr, status, error){
        console.log(error);
        // Otherwise, we probably got a 403 error (forbidden) which indicates the password was bad.
        if(error == "FORBIDDEN"){
            $("#password-header").html("<h2>Incorrect Password</h2>");
        }
    });
}
