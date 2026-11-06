import SwiftUI

struct ContentView: View {
    var body: some View {
        VStack(spacing: 14) {
            Image(systemName: "iphone")
                .font(.system(size: 48))
            Text("OldiOS")
                .font(.largeTitle.bold())
            Text("ARMv6 compatibility runtime")
                .font(.headline)
            Text("The iOS host shell is ready for integration. The emulator core and UIKit compatibility layer are not connected yet.")
                .multilineTextAlignment(.center)
                .foregroundStyle(.secondary)
                .padding(.horizontal)
            Text("Prototype — no legacy apps can be launched yet.")
                .font(.footnote)
                .foregroundStyle(.orange)
        }
        .padding()
    }
}
