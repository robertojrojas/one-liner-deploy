#!/usr/bin/env python
# -- coding: utf-8 --

import time
import os
import boto3

VPC_CIDR = '192.168.0.0/16'
VPC_NAME = 'oneliner'
## eu-west-1
UBUNTU_AMI = 'ami-8fd760f6'
## us-east-1
#UBUNTU_AMI = 'ami-13c15e69'
EC2_TYPE = 't2.medium'
KEY_NAME = 'oneliner-key'
POLL_TIMES = 120



def create_vpc(ec2):
    """Create VPC and return ID."""
    #print 'creating VPC...'
    vpc = ec2.create_vpc(CidrBlock=VPC_CIDR)
    name_resource(vpc, VPC_NAME+'-vpc')
    vpc.wait_until_available()
    #print 'VCP_ID: {}'.format(vpc.id)
    return vpc

def dhcp_option_sets(ec2, vpc):
    #print 'DHCP option sets...'
    dhcp_options = ec2.create_dhcp_options(
        DhcpConfigurations=[
            {
                'Key': 'domain-name',
                'Values': [
                    'ec2.internal',
                ]
            },
            {
                'Key': 'domain-name-servers',
                'Values': [
                    'AmazonProvidedDNS',
                ]
            },
        ]
    )
    response = vpc.associate_dhcp_options(
        DhcpOptionsId=dhcp_options.id
    )
    name_resource(dhcp_options, 'dhcp_options_onliner')


def create_subnets(ec2, client, vpc):
    #print 'creating Subnets...'
    subnets = []
    cidr_id = 1
    for az in client.describe_availability_zones()['AvailabilityZones']:
        az_name = az['ZoneName']
        cidr = '192.168.{}.0/24'.format(cidr_id)
        subnet = ec2.create_subnet(AvailabilityZone=az_name, CidrBlock=cidr, VpcId=vpc.id)
        subnets.append(subnet)
        time.sleep(2)
        name_resource(subnet, VPC_NAME+'-sub-'+az_name)
        cidr_id = cidr_id + 1
    return subnets

def create_internet_gateway(ec2, vpc):
    #print 'creating Internet Gateway...'
    gateway = ec2.create_internet_gateway()
    gateway.attach_to_vpc(VpcId=vpc.id)
    name_resource(gateway, VPC_NAME+'-igw')
    return gateway


def create_security_group(ec2, client, vpc):
    #print 'creating Security Group...'
    my_ip = get_my_ip()
    my_ip_cidr = '{}/32'.format(my_ip)
    security_group = ec2.create_security_group(
        Description='oneliner 8000/22',
        GroupName='oneliner-sg',
        VpcId=vpc.id
    )
    security_group.authorize_ingress(
        CidrIp=my_ip_cidr,
        IpProtocol='tcp',
        FromPort=8000,
        ToPort=8000
    )
    security_group.authorize_ingress(
        CidrIp=my_ip_cidr,
        IpProtocol='tcp',
        FromPort=22,
        ToPort=22
    )
    name_resource(security_group, 'oneliner-sg')
    return security_group


def create_custom_route_table(ec2, client, vpc, igw, subnet):
    #print 'creating Custom Route Table...'
    route_table = ec2.create_route_table(
        VpcId=vpc.id
    )
    route = route_table.create_route(
        DestinationCidrBlock='0.0.0.0/0',
        GatewayId=igw.id,
    )
    response = client.associate_route_table(
        RouteTableId=route_table.id,
        SubnetId=subnet.id
    )


def get_my_ip():
    from urllib2 import urlopen
    my_ip = urlopen('http://ipecho.net/plain').read()
    return my_ip


def create_ssh_key_pair(ec2):
    #print 'creating SSH Key Pair...'
    try:
        key_pair = ec2.KeyPair(KEY_NAME)
        key_pair.delete()
    except:
        print "Key pair was not found, but we'll create a new one!"

    key_pair = ec2.create_key_pair(
        KeyName=KEY_NAME
    )
    write_file('oneliner-key.pem', key_pair.key_material)


def run_ec2_instance(ec2, sec_group, subnet):
    #print 'Running Instance...'

    install_py_script = '''#!/bin/bash
    echo 'Installing python...'
    apt-get update -y && apt-get install -y python
    echo 'Python installed!'
    '''
    instances = ec2.create_instances(
    ImageId=UBUNTU_AMI, InstanceType=EC2_TYPE, MaxCount=1, MinCount=1,
    KeyName=KEY_NAME,
    UserData=install_py_script,
    NetworkInterfaces=[{'SubnetId': subnet.id, 'DeviceIndex': 0,
        'AssociatePublicIpAddress': True,
         'Groups': [sec_group.group_id]}])

    instance = instances[0]
    instance.wait_until_running()
    #print 'INSTANCE_ID: {}'.format(instance.id)
    return instance


def wait_ec2_complete(client, instance_id):
    """
    Need to figure out how to wait until instance is up and UserData has been executed
    """
    #print 'Waiting for instance to become ready...'
    time.sleep(60)


def get_public_ip(client, ec2_instance):
    response = client.describe_instances(
        InstanceIds=[
            ec2_instance.id,
        ]
    )
    pub_ip = response['Reservations'][0]['Instances'][0]['PublicIpAddress']
    print '###### SAMPLE APP URL: http://{}:8000 ######'.format(pub_ip)
    write_file('inventory','ol ansible_user=ubuntu ansible_ssh_host={} ansible_ssh_port=22 ansible_ssh_private_key_file=./oneliner-key.pem'.format(pub_ip))


def name_resource(resource, name):
    resource.create_tags(Tags=[{"Key": "Name", "Value": name}])


def write_file(filename, contents):
    kpf = open(filename,'w')
    kpf.write(contents)
    kpf.close()


def check_required_env_vars():
    from os import environ
    required_env_vars = ['AWS_DEFAULT_REGION','AWS_ACCESS_KEY_ID', 'AWS_SECRET_ACCESS_KEY']
    for req_var in required_env_vars:
        if environ.get(req_var) is None:
            print "Missing environment variable {}".format(req_var)
            import sys
            sys.exit(255)


def main():

    check_required_env_vars()

    region_name=os.environ['AWS_DEFAULT_REGION']
    aws_access_key_id=os.environ['AWS_ACCESS_KEY_ID']
    aws_secret_access_key=os.environ['AWS_SECRET_ACCESS_KEY']

    ec2 = boto3.resource('ec2', aws_access_key_id=aws_access_key_id,
                         aws_secret_access_key=aws_secret_access_key,
                         region_name=region_name)
    client = boto3.client('ec2', aws_access_key_id=aws_access_key_id,
                         aws_secret_access_key=aws_secret_access_key,
                         region_name=region_name)

    # 1- VPC
    vpc = create_vpc(ec2)
    # 2- DHCP Options
    dhcp_option_sets(ec2, vpc)
    # 3- subnet
    subnets = create_subnets(ec2, client, vpc)
    # 4- Internet Gateway
    igw = create_internet_gateway(ec2, vpc)
    # 5- Route Table
    create_custom_route_table(ec2, client, vpc, igw, subnets[0])
    # 6- Security Group
    sg = create_security_group(ec2, client, vpc)
    # 7- SSH Key Pair
    create_ssh_key_pair(ec2)
    # 8- Run Instance
    ec2_instance = run_ec2_instance(ec2, sg, subnets[0])

    wait_ec2_complete(client, ec2_instance.id)

    # 9- Get Instance Pub IP
    get_public_ip(client, ec2_instance)

    print 'CLEANUP: ./cleanup.sh {0} {1}'.format(vpc.id, ec2_instance.id)


if __name__ == "__main__":
    main()
