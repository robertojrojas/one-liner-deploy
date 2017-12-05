# one-liner-deploy
One way to deploy a sample application to a new VPC in AWS.

This projects show one approach to provision a VPC with all the configuration needed to deploy a sample Dockerized application.

It uses Python and Boto3 to provision the VPC and Ansible to configure and install the sample application.

The Sample application is [GoTTY](https://github.com/yudai/gotty) - Share your terminal as a web application


## Software Dependencies

* Python 2.7+
* Boto3
* Ansible 2.4+
* AWS CLI
* jq

## Required Environment Variables
```
export AWS_ACCESS_KEY_ID='AWS_ACCESS_KEY_ID'
export AWS_SECRET_ACCESS_KEY='AWS_SECRET_ACCESS_KEY'
export AWS_DEFAULT_REGION=eu-west-1
```


## To Run

```
./onliner.sh
```

Once the script finishes and if everything went well, you should be able to
find `###### SAMPLE APP URL: http://<IP>:8000 ######`, navigate to that URL on your browser.

## To Cleanup

The output will also show the command needed to remove the VPC and all its components.
Something like:

```
./cleanup.sh vpc-xxxx i-xxxxxxx
```


Enjoy!
