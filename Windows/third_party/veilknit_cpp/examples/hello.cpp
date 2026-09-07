#include <veilknit/veilknit.hpp>

#include <filesystem>
#include <iostream>

int main() {
    try {
        constexpr const char* app_id = "veilknit.examples.hello";
        veilknit::Credential credential;
        try {
            credential = veilknit::Client::load_discovered_credential(app_id);
        } catch (const veilknit::Error& error) {
            if (error.code() != "credential_not_found") throw;
            const auto endpoint = veilknit::Client::discover_endpoint();
            std::cout << "Authorization requested. In the daemon console, approve the pending app request.\n";
            credential = veilknit::Client::register_app(endpoint, app_id, "VeilKnit C++ Hello");
            const auto path = std::filesystem::path("app_credentials") / (std::string(app_id) + ".json");
            credential.save(path);
            std::cout << "Credential saved to " << path.string() << "\n";
        }

        auto client = veilknit::Client::authenticate(credential);
        const auto identity = client.identity();
        std::cout << "Hello, " << identity.username << "\n";
        std::cout << "Main DHT: " << identity.main_dht << "\n";
        std::cout << "Authenticated app: " << client.session().app_id << "\n";
        return 0;
    } catch (const veilknit::Error& error) {
        std::cerr << "VeilKnit error [" << error.code() << "]: " << error.what() << "\n";
        return 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
