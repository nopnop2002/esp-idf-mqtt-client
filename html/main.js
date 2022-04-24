//document.getElementById("datetime").innerHTML = "WebSocket is not connected";

var websocket = new WebSocket('ws://'+location.hostname+'/');

function getTextValueByName(name) {
	var textbox = document.getElementsByName(name)
	//console.log('name=', name);
	//console.log('textbox=', textbox);
	//console.log('textbox.length=', textbox.length);
	ret = new Array();
	for (var i=0;i<textbox.length;i++) {
		//console.log('textbox[%d].value=%s', i, textbox[i].value);
		ret[i] = textbox[i].value;
	}
	//console.log('typeof(ret)=', typeof(ret));
	//console.log('ret=', ret);
	return ret;
}

function sendText(name) {
	console.log('sendText');
/*
	var array = ["11", "22", "33"];
	var data = {};
	//data["foo"] = "abc";
	data["foo"] = array;
	var array = ["aa"];
	data["bar"] = array;
	data["hoge"] = 100;
	json_data = JSON.stringify(data);
	console.log(json_data);
*/

	var data = {};
	data["id"] = name;
	data["host"] = getTextValueByName("host");
	console.log('data=', data);
	data["port"] = getTextValueByName("port");
	data["clientId"] = getTextValueByName("clientId");
	data["username"] = getTextValueByName("username");
	data["password"] = getTextValueByName("password");
	data["topic"] = getTextValueByName("topic");
	data["qos"] = getTextValueByName("qos");
	data["payload"] = getTextValueByName("payload");
	console.log('data=', data);
	json_data = JSON.stringify(data);
	console.log('json_data=' + json_data);
	websocket.send(json_data);

/*
	var data = {};
	data["id"] = "button";
	data["name"] = name;
	console.log('name=', name);
	console.log('data=', data);
	json_data = JSON.stringify(data);
	console.log('json_data=' + json_data);
	websocket.send(json_data);
*/
}

websocket.onopen = function(evt) {
	console.log('WebSocket connection opened');
	var data = {};
	data["id"] = "init";
	console.log('data=', data);
	json_data = JSON.stringify(data);
	console.log('json_data=' + json_data);
	websocket.send(json_data);
	//document.getElementById("datetime").innerHTML = "WebSocket is connected!";
}

websocket.onmessage = function(evt) {
	var msg = evt.data;
	console.log("msg=" + msg);
	var values = msg.split('\4'); // \4 is EOT
	console.log("values=" + values);
	switch(values[0]) {
		case 'ID':
			console.log("ID values[1]=" + values[1]);
			console.log("ID values[2]=" + values[2]);
			console.log("ID values[3]=" + values[3]);
			if (values[2] == "value") document.getElementById(values[1]).innerHTML = values[3];
			if (values[2] == "bcolor") document.getElementById(values[1]).style.backgroundColor = values[3];
			break;

		case 'MQTT':
			console.log("MQTT values[1]=" + values[1]);
			console.log("MQTT values[2]=" + values[2]);
			const msg = document.createElement('div')
			msg.className = 'message-body';
			msg.innerText = values[2] + '\nOn topic: ' + values[1];
			document.getElementById('article').appendChild(msg);
			break;

/*
		case 'NAME':
			console.log("NAME values[1]=" + values[1]);
			console.log("NAME values[2]=" + values[2]);
			console.log("NAME values[3]=" + values[3]);
			var textbox = document.getElementsByName(values[1]);
			console.log('textbox[0]=', textbox[0]);
			textbox[0].value = values[3];
			break;
*/
		default:
			break;
	}
}

websocket.onclose = function(evt) {
	console.log('Websocket connection closed');
	//document.getElementById("datetime").innerHTML = "WebSocket closed";
}

websocket.onerror = function(evt) {
	console.log('Websocket error: ' + evt);
	//document.getElementById("datetime").innerHTML = "WebSocket error????!!!1!!";
}
