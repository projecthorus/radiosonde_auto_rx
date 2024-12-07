// auto_rx API Helpers

function update_task_list(){
    // Grab the latest task list.
    $.getJSON("get_task_list", function(data){
        var task_summary = "";
        var task_details = "";
        var num_tasks = 0;

        $('#stop-frequency-select').children().remove();

        added_decoders = false;

        for (_task in data){
            num_tasks += 1;
            // Append the current task to the task list.
            if(_task.includes("SPY") || _task.includes("KA9Q")){
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

                task_icon = "🟢";
                task_detail += (parseFloat( data[_task]["freq"] )/1e6).toFixed(3);

                if (data[_task].hasOwnProperty("type")){
                    task_detail += " " + data[_task]["type"];
                }
                
            } else {
                if(data[_task]["task"] == "Scanning"){
                    task_icon = "🔵";
                    task_detail += "Scan";
                } else {
                    task_icon = "⚪";
                    task_detail += "Idle";
                }
            }

            task_summary += "<span title='" + task_detail + "'>" + task_icon + "</span>"
            task_details += "<span class='sdrinfo-element'>" + task_icon + " " + task_detail + "</span>"
        }

        if(added_decoders == false){
            $('#stop-frequency-select')
                    .append($("<option></option>")
                    .attr("value","0")
                    .text("No Decoders"));
        }
        
        // Update page with latest task.
        if (num_tasks <= 3) {
            $('#summary_element').css("display", "block");
            $('#task_summary').html(task_details);
            $('#task_details').html("");
        } else {
            $('#summary_element').css("display", "list-item");
            $('#task_summary').html(task_summary);
            $('#task_details').html(task_details);
        }
        
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
    $("#azimuth-input").prop('disabled', true);
    $("#elevation-input").prop('disabled', true);
    $("#move-rotator").prop('disabled', true);
    $("#home-rotator").prop('disabled', true);
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
    $("#azimuth-input").prop('disabled', false);
    $("#elevation-input").prop('disabled', false);
    $("#move-rotator").prop('disabled', false);
    $("#home-rotator").prop('disabled', false);
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
            if(autorx_config["rotator_enabled"] == true) {
                $("#rotatorControlForm").show().css("visibility","visible");
            } else {
                $("#rotatorControlForm").hide().css("visibility","hidden");
            }
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

    // Parse to a float.
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

function move_rotator(){
    // Move rotator to requested position

    // Re-verify the password. This will occur async, so wont stop the main request from going ahead,
    // but will at least present an error for the user.
    verify_password();

    // Grab the password
    _api_password = getCookie("password");

    // Grab the az/el input
    _az = $('#azimuth-input').val();
    _el = $('#elevation-input').val();

    // Parse to a float.
    _az_float = parseFloat(_az);
    if(_az_float > 360){
        alert("Supplied azimuth above 360 degrees");
        return;
    }
    if(_az_float < 0){
        alert("Supplied azimuth below 0 degrees");
        return;
    }

    _el_float = parseFloat(_el);
    if(_el_float > 90){
        alert("Supplied elevation above 360 degrees");
        return;
    }
    if(_el_float < 0){
        alert("Supplied elevation below 0 degrees");
        return;
    }

    // Do the request
    $.post(
        "move_rotator", 
        {password: _api_password, az: _az_float.toFixed(1), el: _el_float.toFixed(1)},
        function(data){
            alert("Moving rotator to " + _az + ", " + _el + ".");
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

function home_rotator(){
    // Home rotator

    // Re-verify the password. This will occur async, so wont stop the main request from going ahead,
    // but will at least present an error for the user.
    verify_password();

    // Grab the password
    _api_password = getCookie("password");

    // Do the request
    $.post(
        "home_rotator", 
        {password: _api_password},
        function(data){
            alert("Homing rotator.");
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