# /etc/ubus daemon 

This daemon makes it easy to organize ubus objects into a cleaner structure -
much like sysfs filesystem and dbus. You can place all your ubus objects into
/etc/ubus folder tree and access them through both symlinks and filesystem-like
paths. 

This allows you to write ubus objects in shell, C, lua or whatever else you
want. Then you place them somewhere under /etc/ubus/ and reload the service and
they will come up as ubus objects. 

# Example 

* Create a shell script in /etc/ubus/mynamespace/test

	#!/bin/sh

	if [ "$1" == ".methods" ]; then echo "test"; fi
	if [ "$1" == "test" ]; then echo "{\"foo\":\"bar\"}"; fi

* Make it executable (chmod +x ..) 
* /etc/init.d/etc-ubus-daemon reload 
* if you want, you can also create a symlink to your object

	(cd /etc/ubus; ln -s mynamespace/test link) 

* ubus list -v /mynamespace*
	
	'/link' @1c6da7c7
		"test":{}
	'/mynamespace/test' @ef4f806d
		"test":{}

You can now call these objects just like any other ubus object. 

# paramter specification

The .methods metamethod can either return a comma separated string of methods, or it can return a json object that looks like this: 

	{ 
		"method": { "param": "param_type" }
		..
	}

This allows you to set let ubus know what type the paramteres should have. Valid types are: 

	bool
	int
	string


