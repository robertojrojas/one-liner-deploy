
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
#include <aws/ec2/model/CreateRouteTableRequest.h>
#include <aws/ec2/model/AssociateRouteTableRequest.h>
#include <aws/ec2/model/CreateRouteRequest.h>
#include <aws/ec2/model/CreateSecurityGroupRequest.h>
#include <aws/ec2/model/AuthorizeSecurityGroupIngressRequest.h>
#include <aws/ec2/model/CreateKeyPairRequest.h>
#include <aws/ec2/model/RunInstancesRequest.h>
#include <aws/ec2/model/DescribeInstancesRequest.h>
#include <aws/ec2/model/InstanceNetworkInterfaceSpecification.h>
#include <aws/ec2/model/DeleteKeyPairRequest.h>
#include <aws/ec2/model/DescribeInstanceStatusRequest.h>
#include <aws/ec2/model/DescribeImagesRequest.h>
#include <aws/core/utils/HashingUtils.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <unordered_map>
#include <curl/curl.h>

#define ANYWHERE  "0.0.0.0/0"
#define CIDR_BLOCK "192.168.0.0/16"
#define SUBNET_CIDR_PREFIX  "192.168." 
#define SUBNET_CIDR_SUFFIX  ".0/24"
#define PROTOCOL "tcp"
#define EMPTY ""
#define NO_ERROR EMPTY
// TODO: Figure out concat strings
#define VPC_NAME_PREFIX  "oneliner"
#define VPC_NAME "oneliner-vpc" 
#define SECURITY_GROUP_DESCRITPION "oneliner 8000/22"
#define SECURITY_GROUP_NAME  "oneliner-sg"
#define MYIP_SERVICE_URL "http://ipecho.net/plain"
#define	KEY_PAIR_NAME "oneliner-key"
#define INSTANCE_NAME "oneliner-instance"
#define UBUNTU_XENIAL_QUERY_STR "ubuntu/images/hvm-ssd/ubuntu-xenial-16.04-amd64*"
#define INSTALL_PYTHON_SCRIPT "install_python.sh"


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

const Aws::String createCustomRouteTable(const Aws::EC2::EC2Client &ec2, 
                     const Aws::String& vpcId, const Aws::String& igwId, const Aws::String& subnetId) {
    
    Aws::EC2::Model::CreateRouteTableRequest createRouteTableRequest;
    createRouteTableRequest.SetVpcId(vpcId);
    auto createRouteTableOutcome = ec2.CreateRouteTable(createRouteTableRequest);
    if (!createRouteTableOutcome.IsSuccess()) {
        return createRouteTableOutcome.GetError().GetMessage();
    }
    auto routeTableId = createRouteTableOutcome.GetResult().GetRouteTable().GetRouteTableId();
    
    Aws::EC2::Model::CreateRouteRequest createRouteRequest;
    createRouteRequest.SetRouteTableId(routeTableId);
    createRouteRequest.SetNatGatewayId(igwId);
    createRouteRequest.SetDestinationCidrBlock(ANYWHERE);
    auto createRouteOutcome = ec2.CreateRoute(createRouteRequest);
    if (!createRouteOutcome.IsSuccess()) {
        return createRouteOutcome.GetError().GetMessage();
    }
    
    Aws::EC2::Model::AssociateRouteTableRequest associateRouteTableRequest;
    associateRouteTableRequest.SetRouteTableId(routeTableId);
    associateRouteTableRequest.SetSubnetId(subnetId);
    auto associateRouteTableOutcome = ec2.AssociateRouteTable(associateRouteTableRequest);
    if (!associateRouteTableOutcome.IsSuccess()) {
        return associateRouteTableOutcome.GetError().GetMessage();
    }
   return NO_ERROR;
}


static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

const std::tuple<const std::string , const std::string >  getMyIP() {
    std::string readBuffer;
    CURL *curl;
    CURLcode res;
    curl = curl_easy_init();
    if(!curl) {
        return std::make_tuple(EMPTY, "Error initializing CURL!");
    }

    curl_easy_setopt(curl, CURLOPT_URL, MYIP_SERVICE_URL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

    /* Perform the request, res will get the return code */ 
    res = curl_easy_perform(curl);
    /* Check for errors */ 
    if(res != CURLE_OK) {
        std::stringstream hse;
        hse << "curl_easy_perform() failed: " <<  curl_easy_strerror(res);
        return std::make_tuple(EMPTY, hse.str());
    }

    /* always cleanup */ 
    curl_easy_cleanup(curl);

    return std::make_tuple(readBuffer, EMPTY);
}


std::tuple<const Aws::String , const Aws::String> createSecurityGroup(const Aws::EC2::EC2Client &ec2, 
                                        const Aws::String& vpcId)
{
    Aws::EC2::Model::CreateSecurityGroupRequest createSecurityGroupRequest;
    createSecurityGroupRequest.SetVpcId(vpcId);
    std::stringstream gnss;
    gnss << VPC_NAME_PREFIX << "-sg" ;
    createSecurityGroupRequest.SetGroupName(gnss.str().c_str());
    createSecurityGroupRequest.SetDescription(SECURITY_GROUP_DESCRITPION);
    auto createSecurityGroupOutcome = ec2.CreateSecurityGroup(createSecurityGroupRequest);
    if (!createSecurityGroupOutcome.IsSuccess()) {
        return std::make_tuple(EMPTY, createSecurityGroupOutcome.GetError().GetMessage());
    }
    auto sgId = createSecurityGroupOutcome.GetResult().GetGroupId();
    auto myIPResult = getMyIP();
    if (std::get<1>(myIPResult) != NO_ERROR) {
        Aws::String myIPError(std::get<1>(myIPResult).c_str());
        return std::make_tuple(EMPTY, myIPError);
    }
    auto myIP = std::get<0>(myIPResult);
    std::stringstream mcss;
    mcss << myIP << "/32";

    std::cout << "My IP: " << myIP << std::endl;

    Aws::EC2::Model::AuthorizeSecurityGroupIngressRequest authorizeSecurityGroupIngressRequest8000;
    authorizeSecurityGroupIngressRequest8000.SetCidrIp(mcss.str().c_str());
    authorizeSecurityGroupIngressRequest8000.SetIpProtocol(PROTOCOL);
    authorizeSecurityGroupIngressRequest8000.SetFromPort(8000);
    authorizeSecurityGroupIngressRequest8000.SetToPort(8000);
    authorizeSecurityGroupIngressRequest8000.SetGroupId(sgId);
    auto a8000Outcome = ec2.AuthorizeSecurityGroupIngress(authorizeSecurityGroupIngressRequest8000);
    if (!a8000Outcome.IsSuccess()) {
        return std::make_tuple(EMPTY, a8000Outcome.GetError().GetMessage());
    }

    Aws::EC2::Model::AuthorizeSecurityGroupIngressRequest authorizeSecurityGroupIngressRequest22;
    authorizeSecurityGroupIngressRequest22.SetCidrIp(mcss.str().c_str());
    authorizeSecurityGroupIngressRequest22.SetIpProtocol(PROTOCOL);
    authorizeSecurityGroupIngressRequest22.SetFromPort(22);
    authorizeSecurityGroupIngressRequest22.SetToPort(22);
    authorizeSecurityGroupIngressRequest22.SetGroupId(sgId);
    auto a22utcome =ec2.AuthorizeSecurityGroupIngress(authorizeSecurityGroupIngressRequest22);
    if (!a22utcome.IsSuccess()) {
        return std::make_tuple(EMPTY, a22utcome.GetError().GetMessage());
    }

    auto ret = nameResource(ec2, SECURITY_GROUP_NAME, sgId);
    if (ret != NO_ERROR) {
        return std::make_tuple(EMPTY, ret);
    }
    return std::make_tuple(sgId, EMPTY);
}


const std::string writeFile(const std::string& filename, const std::string& content) {
    std::ofstream destFile;
    destFile.open (filename);
    destFile << content;
    destFile.close();
    return NO_ERROR;
}

const std::string buildKeyNameFilename() {
    std::stringstream sfile;
    sfile << KEY_PAIR_NAME << ".pem";
    return sfile.str();
}

const Aws::String createSSHKeyPair(const Aws::EC2::EC2Client &ec2) {

    Aws::EC2::Model::DeleteKeyPairRequest del_request;
    del_request.SetKeyName(KEY_PAIR_NAME);
    auto dloutcome = ec2.DeleteKeyPair(del_request);
    if (!dloutcome.IsSuccess())
    {
       return dloutcome.GetError().GetMessage();
    }

    Aws::EC2::Model::CreateKeyPairRequest request;
    request.SetKeyName(KEY_PAIR_NAME);

    auto outcome = ec2.CreateKeyPair(request);
    if (!outcome.IsSuccess())
    {
       return outcome.GetError().GetMessage();
    }
    auto keyMaterial = outcome.GetResult().GetKeyMaterial();
    auto ret = writeFile(buildKeyNameFilename(), keyMaterial.c_str());
    if (ret != NO_ERROR) {
        return ret.c_str();
    }
    return NO_ERROR;
}


const std::string readFile(const std::string& filename) {
    std::ifstream inFile (filename);
    std::string line;
    std::stringstream data;
    if (!inFile.is_open())
    {
        return EMPTY;
    }
    while ( getline (inFile,line) )
    {
        data << line << '\n';
    }
    inFile.close();
    return data.str();
}

bool sortByCreationDate(const Aws::EC2::Model::Image& left, const Aws::EC2::Model::Image& right) { 
    Aws::Utils::DateTime leftCreateDate(left.GetCreationDate(), Aws::Utils::DateFormat::ISO_8601); 
    Aws::Utils::DateTime rightCreateDate(right.GetCreationDate(), Aws::Utils::DateFormat::ISO_8601); 
    return leftCreateDate > rightCreateDate; 
}

std::tuple<const Aws::String , const Aws::String> findUbuntuAMI(const Aws::EC2::EC2Client &ec2) {
    Aws::EC2::Model::DescribeImagesRequest request;
    Aws::EC2::Model::Filter ubuntuFilter;
    ubuntuFilter.SetName("name");
    ubuntuFilter.AddValues(UBUNTU_XENIAL_QUERY_STR);
    Aws::Vector<Aws::EC2::Model::Filter> filters;
    filters.push_back(ubuntuFilter);
    request.SetFilters(filters);
    auto outcome =  ec2.DescribeImages(request);
    if (!outcome.IsSuccess()) {
       return std::make_tuple(EMPTY, outcome.GetError().GetMessage());
    }
    auto images = outcome.GetResult().GetImages();
    std::sort(images.begin(), images.end(), sortByCreationDate);
    auto latestUbuntuAMI = images[0].GetImageId();
    return std::make_tuple(latestUbuntuAMI, EMPTY);
}

std::tuple<const Aws::String , const Aws::String> runInstance(const Aws::EC2::EC2Client &ec2, 
                const Aws::String& secGroup, const Aws::String& subnetId) {

    Aws::EC2::Model::RunInstancesRequest runInstancesRequest;
    auto findAmiRet = findUbuntuAMI(ec2);
    if (std::get<1>(findAmiRet) != NO_ERROR) {
        return std::make_tuple(EMPTY, std::get<1>(findAmiRet));
    }
    auto ubuntuAMI = std::get<0>(findAmiRet);
    std::cout << "Using AMI: " << ubuntuAMI << std::endl;
    Aws::String ami(ubuntuAMI.c_str());
    runInstancesRequest.SetImageId(ami);
    runInstancesRequest.SetInstanceType(Aws::EC2::Model::InstanceType::t2_medium);
    runInstancesRequest.SetMinCount(1);
    runInstancesRequest.SetMaxCount(1);

    auto userDataScript = readFile(INSTALL_PYTHON_SCRIPT);
    Aws::String userdata = Aws::Utils::HashingUtils::Base64Encode(
            Aws::Utils::ByteBuffer((unsigned char*)userDataScript.c_str(), userDataScript.size()));
    runInstancesRequest.SetUserData(userdata);

    Aws::String kn(KEY_PAIR_NAME);
    runInstancesRequest.SetKeyName(kn);
    Aws::Vector<Aws::EC2::Model::InstanceNetworkInterfaceSpecification> nicSpecs;
    Aws::EC2::Model::InstanceNetworkInterfaceSpecification nicSpec;
    nicSpec.SetDeviceIndex(0);
    nicSpec.SetSubnetId(subnetId);
    nicSpec.AddGroups(secGroup);
    nicSpec.SetAssociatePublicIpAddress(true);
    nicSpecs.push_back(nicSpec);
    runInstancesRequest.SetNetworkInterfaces(nicSpecs);

    auto run_outcome = ec2.RunInstances(runInstancesRequest);
    if (!run_outcome.IsSuccess())
    {
        return std::make_tuple(EMPTY, run_outcome.GetError().GetMessage());
    }

    const auto& instances = run_outcome.GetResult().GetInstances();
    if (instances.size() == 0)
    {
        std::stringstream se;
        se << "Failed to start ec2 instance based on ami " << ubuntuAMI;
        return std::make_tuple(EMPTY, se.str().c_str());
    }

    auto instance_id = instances[0].GetInstanceId();
    std::cout << "Waiting for EC2 " << instance_id << " instance to start running " << std::endl;
 
     while(true) {
        std::this_thread::sleep_for(std::chrono::seconds(15));
        Aws::EC2::Model::DescribeInstanceStatusRequest request;
        request.AddInstanceIds(instance_id);

        auto outcome = ec2.DescribeInstanceStatus(request);
        if (!outcome.IsSuccess())
        {
            return std::make_tuple(EMPTY, outcome.GetError().GetMessage());
        }

        // Instance is probably coming up - try later
        if (outcome.GetResult().GetInstanceStatuses().size() == 0) {
            continue;    
        }
        auto status = outcome.GetResult().GetInstanceStatuses()[0].GetInstanceStatus().GetStatus();
        if (status == Aws::EC2::Model::SummaryStatus::ok) {
            break;
        }
    }

    auto nameResourceRet = nameResource(ec2, INSTANCE_NAME, instance_id);
    if (nameResourceRet != NO_ERROR)
    {
      std::make_tuple(EMPTY, nameResourceRet);
    }
    return std::make_tuple(instance_id, EMPTY);
}


std::tuple<const Aws::String , const Aws::String> getInstancePublicIP(const Aws::EC2::EC2Client &ec2, const Aws::String instanceId) {
    Aws::EC2::Model::DescribeInstancesRequest request;
    request.AddInstanceIds(instanceId);

    auto outcome = ec2.DescribeInstances(request);
    if (!outcome.IsSuccess())
    {
       return std::make_tuple(EMPTY, outcome.GetError().GetMessage());
    }
    auto pubIP = outcome.GetResult().GetReservations()[0].GetInstances()[0].GetPublicIpAddress();
    return std::make_tuple(pubIP, EMPTY);
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

        auto routeTableResult = createCustomRouteTable(ec2, vpcId, igwId, subnetId);
        if (routeTableResult != NO_ERROR) {
            std::cout << "Failed to create Route Table " <<
            routeTableResult << std::endl;
            return 1;
        }

        auto sgResult = createSecurityGroup(ec2, vpcId);
        if (std::get<1>(sgResult) != NO_ERROR) {
            std::cout << "Failed to create Security Group " <<
            std::get<1>(sgResult) << std::endl;
            return 1;
        }
        auto sgId = std::get<0>(sgResult);
        std::cout << "Create Security Group Id: " << sgId << std::endl;

        auto ret = createSSHKeyPair(ec2);
        if (ret != NO_ERROR) {
            std::cout << "Failed to create Key Pair " <<
            ret << std::endl;
            return 1;
        }

        auto runInstanceRet = runInstance(ec2, sgId, subnetId);
        if (std::get<1>(runInstanceRet) != NO_ERROR) {
            std::cout << "Failed to run instance " <<
            std::get<1>(runInstanceRet) << std::endl;
            return 1;
        }
        auto instanceId = std::get<0>(runInstanceRet);
        std::cout << "Instance id: " << instanceId << std::endl;

        auto pubIPRet = getInstancePublicIP(ec2, instanceId);
        if (std::get<1>(pubIPRet) != NO_ERROR) {
            std::cout << "Failed to get instance Public IP " <<
            std::get<1>(pubIPRet) << std::endl;
            return 1;
        }
        auto pubIP =  std::get<0>(pubIPRet);
        std::cout << "###### SAMPLE APP URL: http://" << pubIP << ":8000 ######" << std::endl;

        std::stringstream icss;
        icss << "ol ansible_user=ubuntu ansible_ssh_host=" << pubIP << " ansible_ssh_port=22 ansible_ssh_private_key_file=./" << buildKeyNameFilename() ;
        auto wfRet = writeFile("inventory", icss.str());
        if (wfRet != NO_ERROR) {
            std::cout << "Failed to create Inventory file " <<
            wfRet << std::endl;
            return 1;
        }
        std::cout << "CLEANUP: ./cleanup.sh " << vpcId << " " << instanceId << std::endl;
    }    

    Aws::ShutdownAPI(options);
    return 0;
}