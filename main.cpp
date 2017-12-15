
#include <aws/core/Aws.h>
#include <aws/ec2/EC2Client.h>
#include <aws/ec2/model/CreateVpcRequest.h>
#include <aws/ec2/model/CreateTagsRequest.h>
#include <aws/ec2/model/DescribeVpcsRequest.h>
#include <aws/ec2/model/DhcpConfiguration.h>
#include <aws/ec2/model/DhcpOptions.h>
#include <aws/ec2/model/CreateDhcpOptionsRequest.h>
#include <aws/ec2/model/AssociateDhcpOptionsRequest.h>
#include <aws/ec2/model/NewDhcpConfiguration.h>
#include <aws/ec2/model/DescribeAvailabilityZonesRequest.h>
#include <aws/ec2/model/CreateSubnetRequest.h>
#include <aws/ec2/model/DescribeSubnetsRequest.h>
#include <aws/ec2/model/CreateInternetGatewayRequest.h>
#include <aws/ec2/model/AttachInternetGatewayRequest.h>
#include <iostream>
#include <sstream>
#include <unordered_map>

#define CIDR_BLOCK "192.168.0.0/16"
#define SUBNET_CIDR_PREFIX  "192.168." 
#define SUBNET_CIDR_SUFFIX  ".0/24"
#define EMPTY ""
#define NO_ERROR EMPTY
#define VPC_NAME_PREFIX  "oneliner"
#define VPC_NAME "oneliner-vpc" // TODO: Figure out concat strings

const Aws::String nameResource(const Aws::EC2::EC2Client &ec2, const Aws::String& tagName, const Aws::String& resourceId) {
    Aws::EC2::Model::Tag name_tag;
    name_tag.SetKey("Name");
    name_tag.SetValue(tagName);

    Aws::EC2::Model::CreateTagsRequest create_request;
    create_request.AddResources(resourceId);
    create_request.AddTags(name_tag);

    auto create_outcome = ec2.CreateTags(create_request);
    if (!create_outcome.IsSuccess())
    {
        return create_outcome.GetError().GetMessage();
    }
    return NO_ERROR;
}

std::tuple<const Aws::String , const Aws::String>createVPC(const Aws::EC2::EC2Client &ec2) {
   Aws::EC2::Model::CreateVpcRequest createVpcReq;
   createVpcReq.SetCidrBlock(CIDR_BLOCK);

   auto createVpcOutcome = ec2.CreateVpc(createVpcReq);
   if (!createVpcOutcome.IsSuccess())
   {
      return std::make_tuple(EMPTY, createVpcOutcome.GetError().GetMessage());
   }
   auto vpcId = createVpcOutcome.GetResult().GetVpc().GetVpcId();

   std::cout << "Waiting for VPC to become available..." << std::endl;
   Aws::EC2::Model::DescribeVpcsRequest describeVpcsReq;
   Aws::Vector<Aws::String> vpcids = {vpcId};
   describeVpcsReq.SetVpcIds(vpcids);
   auto describeVpcsOutcome = ec2.DescribeVpcs(describeVpcsReq);
   if (describeVpcsOutcome.IsSuccess()) {
        while(true) {
            if (describeVpcsOutcome.GetResult().GetVpcs()[0].GetState() == Aws::EC2::Model::VpcState::available) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
   } else {
       return std::make_tuple(EMPTY, describeVpcsOutcome.GetError().GetMessage());
   }

   auto ret = nameResource(ec2, VPC_NAME, vpcId);
   if (ret != NO_ERROR) {
       return std::make_tuple(EMPTY, ret);
   }
   return std::make_tuple(vpcId, EMPTY);
}

const Aws::String dhcpOptionSets(const Aws::EC2::EC2Client &ec2, const Aws::String& vpcId) {
    Aws::EC2::Model::CreateDhcpOptionsRequest createDhcpOptionsRequest;

    Aws::EC2::Model::NewDhcpConfiguration dhcpConfec2Internal;
    dhcpConfec2Internal.SetKey("domain-name");
    Aws::Vector<Aws::String> dhcpConfec2InternalVals;
    dhcpConfec2InternalVals.push_back(Aws::String("ec2.internal"));
    dhcpConfec2Internal.SetValues(dhcpConfec2InternalVals);
    createDhcpOptionsRequest.AddDhcpConfigurations(dhcpConfec2Internal);    

    Aws::EC2::Model::NewDhcpConfiguration dhcpConfAmazonProvidedDNS;
    dhcpConfAmazonProvidedDNS.SetKey("domain-name-servers");
    Aws::Vector<Aws::String> dhcpConfAmazonProvidedDNSVals;
    dhcpConfAmazonProvidedDNSVals.push_back(Aws::String("AmazonProvidedDNS"));
    dhcpConfAmazonProvidedDNS.SetValues(dhcpConfAmazonProvidedDNSVals);
    createDhcpOptionsRequest.AddDhcpConfigurations(dhcpConfAmazonProvidedDNS); 

    std::cout << "creating DHCP Options... " << std::endl;
    auto createDhcpOptionsOutcome = ec2.CreateDhcpOptions(createDhcpOptionsRequest);
    if (!createDhcpOptionsOutcome.IsSuccess()) {
        return createDhcpOptionsOutcome.GetError().GetMessage();
    }
    auto dhcpOptionsId = createDhcpOptionsOutcome.GetResult().GetDhcpOptions().GetDhcpOptionsId();
   
    Aws::EC2::Model::AssociateDhcpOptionsRequest associateDhcpOptionsRequest;
    associateDhcpOptionsRequest.SetVpcId(vpcId);
    associateDhcpOptionsRequest.SetDhcpOptionsId(dhcpOptionsId);
    std::cout << "Associating DHCP Options... " << vpcId << std::endl;
    auto associateDhcpOptionsOutcome = ec2.AssociateDhcpOptions(associateDhcpOptionsRequest);
    if (!associateDhcpOptionsOutcome.IsSuccess()) {
        return associateDhcpOptionsOutcome.GetError().GetMessage();
    }

    std::cout << "Naming DHCP id: " << dhcpOptionsId << std::endl;
    auto ret = nameResource(ec2, "dhcp_options_onliner", dhcpOptionsId);
    if (ret != NO_ERROR) {
       return ret;
    }
    return NO_ERROR;
}


std::tuple<const Aws::Vector<Aws::String> , const Aws::String> createSubnets(const Aws::EC2::EC2Client &ec2, const Aws::String& vpcId) {
    Aws::EC2::Model::DescribeAvailabilityZonesRequest describeRequest;
    auto describeOutcome = ec2.DescribeAvailabilityZones(describeRequest);

    Aws::Vector<Aws::String> subnetIds;
    if (!describeOutcome.IsSuccess()){
        return std::make_tuple(subnetIds, describeOutcome.GetError().GetMessage());
    }
    const auto &zones =
                describeOutcome.GetResult().GetAvailabilityZones();
    int cidrIdx = 1;
    for (const auto &zone : zones)
    {
        std::stringstream subnetCidr;
        subnetCidr << SUBNET_CIDR_PREFIX << cidrIdx++ << SUBNET_CIDR_SUFFIX;
        Aws::EC2::Model::CreateSubnetRequest createSubnetRequest;
        createSubnetRequest.SetAvailabilityZone(zone.GetZoneName());
        createSubnetRequest.SetVpcId(vpcId);
        createSubnetRequest.SetCidrBlock(subnetCidr.str().c_str());
        auto createSubnetOutcome = ec2.CreateSubnet(createSubnetRequest);
        if (!createSubnetOutcome.IsSuccess()) {
            return std::make_tuple(subnetIds, createSubnetOutcome.GetError().GetMessage());
        }
        auto subnetId = createSubnetOutcome.GetResult().GetSubnet().GetSubnetId();
        Aws::EC2::Model::DescribeSubnetsRequest describeSubnetsRequest;
        describeSubnetsRequest.AddSubnetIds(subnetId);
        auto describeSubnetsOutcome = ec2.DescribeSubnets(describeSubnetsRequest);
        if (!describeSubnetsOutcome.IsSuccess()) {
            return std::make_tuple(subnetIds, describeSubnetsOutcome.GetError().GetMessage());
        }

        // TODO: figure out how to wait in a more elegant manner
        std::cout << "Waiting for Subnet (" << subnetId  << ") to become available..." << std::endl;
        while(true) {
            if (describeSubnetsOutcome.GetResult().GetSubnets()[0].GetState() == Aws::EC2::Model::SubnetState::available) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        std::stringstream subnetTagNamess;
        subnetTagNamess << VPC_NAME_PREFIX << "-sub-" << zone.GetZoneName();
        Aws::String subnetTagName(subnetTagNamess.str().c_str()); 
        auto ret = nameResource(ec2, subnetTagName, subnetId);
        if (ret != NO_ERROR) {
            return std::make_tuple(subnetIds, ret);
        }
        subnetIds.push_back(subnetId);

    }
    std::cout << "Done creating subnets" << std::endl;
    return std::make_tuple(subnetIds, EMPTY);
}

std::tuple<const Aws::String , const Aws::String> createInternetGateway(const Aws::EC2::EC2Client &ec2, const Aws::String& vpcId) {
   Aws::EC2::Model::CreateInternetGatewayRequest createInternetGatewayRequest;
   auto createInternetGatewayOutcome = ec2.CreateInternetGateway(createInternetGatewayRequest);
   if (!createInternetGatewayOutcome.IsSuccess()) {
       return std::make_tuple(EMPTY, createInternetGatewayOutcome.GetError().GetMessage());
   }
   auto igwId = createInternetGatewayOutcome.GetResult().GetInternetGateway().GetInternetGatewayId();

   Aws::EC2::Model::AttachInternetGatewayRequest attachInternetGatewayRequest;
   attachInternetGatewayRequest.SetVpcId(vpcId);
   attachInternetGatewayRequest.SetInternetGatewayId(igwId);
   auto attachInternetGatewayOutcome = ec2.AttachInternetGateway(attachInternetGatewayRequest);
   if (!attachInternetGatewayOutcome.IsSuccess()) {
       return std::make_tuple(EMPTY, attachInternetGatewayOutcome.GetError().GetMessage());
   }

   std::stringstream igwss;
   igwss << VPC_NAME_PREFIX << "-igw";
   Aws::String igwTagName(igwss.str().c_str());
   auto ret = nameResource(ec2, igwTagName, igwId);
   if (ret != NO_ERROR) {
     return std::make_tuple(EMPTY, ret);
   }
   return std::make_tuple(igwId,EMPTY);
}


int main(int argc, char** argv) {
    Aws::SDKOptions options;
    Aws::InitAPI(options);
    {
        Aws::EC2::EC2Client ec2;
        auto vpcCreateResult = createVPC(ec2);
        if (std::get<1>(vpcCreateResult) != NO_ERROR) {
            std::cout << "Failed to create VPC "  <<
            std::get<1>(vpcCreateResult) << std::endl;
            return 1;
        }
        const Aws::String& vpcId = std::get<0>(vpcCreateResult);
        std::cout << "Created VPC: " << vpcId << std::endl;

        auto dhcpResult = dhcpOptionSets(ec2, vpcId);
        if (dhcpResult != NO_ERROR) {
            std::cout << "Failed to configure DHCP Sets "  << std::endl;
            return 1;
        }

        auto subnetsResult = createSubnets(ec2, vpcId);
        if (std::get<1>(subnetsResult) != NO_ERROR) {
            std::cout << "Failed to create subnets " <<
            std::get<1>(subnetsResult) << std::endl;
            return 1;
        }
        auto subnetId = std::get<0>(subnetsResult)[0];
        std::cout << "Using SubnetId: " << subnetId << std::endl;

        auto igwResult = createInternetGateway(ec2, vpcId);
        if (std::get<1>(igwResult) != NO_ERROR) {
            std::cout << "Failed to create InternetGateway " <<
            std::get<1>(igwResult) << std::endl;
            return 1;
        }
        auto igwId = std::get<0>(igwResult);
        std::cout << "Create InternetGateway Id: " << igwId << std::endl;

    }    

    Aws::ShutdownAPI(options);
    return 0;
}