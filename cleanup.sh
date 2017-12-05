#!/bin/bash

INSTANCE_ID=$2
VPC_ID=$1

aws ec2 terminate-instances --instance-ids ${INSTANCE_ID}
aws ec2 wait instance-terminated --instance-ids ${INSTANCE_ID}

aws ec2 delete-key-pair --key-name oneliner-key

INTERNET_GATEWAY_ID=$(aws ec2 describe-internet-gateways \
  --filters "Name=tag:Name,Values=oneliner-igw" | \
  jq -r '.InternetGateways[].InternetGatewayId')

aws ec2 detach-internet-gateway \
  --internet-gateway-id ${INTERNET_GATEWAY_ID} \
  --vpc-id ${VPC_ID}

aws ec2 delete-internet-gateway \
  --internet-gateway-id ${INTERNET_GATEWAY_ID}


SECURITY_GROUP_ID=$(aws ec2 describe-security-groups \
  --filters "Name=tag:Name,Values=oneliner-sg" | \
  jq -r '.SecurityGroups[].GroupId')

aws ec2 delete-security-group \
  --group-id ${SECURITY_GROUP_ID}


SUBNETS=$(aws ec2 describe-subnets \
  --filters "Name=vpc-id,Values=${VPC_ID}" | \
  jq -r '.Subnets[].SubnetId')

for SUBNET_ID in $SUBNETS; do aws ec2 delete-subnet --subnet-id ${SUBNET_ID}; done;

ROUTE_TABLES=$(aws ec2 describe-route-tables \
  --filters "Name=vpc-id,Values=${VPC_ID}"| \
  jq -r '.RouteTables[].RouteTableId')

for ROUTE_TABLE_ID in $ROUTE_TABLES; do aws ec2 delete-route-table --route-table-id ${ROUTE_TABLE_ID}; done;

aws ec2 delete-vpc --vpc-id ${VPC_ID}


DHCP_OPTION_SETS=$(aws ec2 describe-dhcp-options \
  --filters "Name=tag:Name,Values=dhcp_options_onliner" | \
  jq -r '.DhcpOptions[].DhcpOptionsId')

for DHCP_OPTION_SET_ID in $DHCP_OPTION_SETS; do aws ec2 delete-dhcp-options \
  --dhcp-options-id ${DHCP_OPTION_SET_ID}; done;
