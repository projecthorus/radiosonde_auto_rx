// auto_rx API Helpers
// 

function update_task_list(){
    // Grab the latest task list.
    $.getJSON("/get_task_list", function(data){
        var task_info = "";
        for (_task in data){
            task_info += "SDR #" + _task + ": " + data[_task] + "    ";
        }
        
        // Update page with latest task.
        $('#task_status').text(task_info);
    });
}