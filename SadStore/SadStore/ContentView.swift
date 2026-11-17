import SwiftUI

struct ContentView: View {
    var body: some View {
        NavigationView {
            VStack(spacing: 16) {
                Image(systemName: "bag")
                    .font(.system(size: 48, weight: .medium))
                    .foregroundColor(.pink)
                Text("SadStore")
                    .font(.largeTitle.bold())
                Text("Добро пожаловать!")
                    .font(.title3)
                    .foregroundColor(.secondary)
                Text("Первый экран приложения готов. Здесь можно будет разместить каталог приложений и другие разделы.")
                    .multilineTextAlignment(.center)
                    .foregroundColor(.secondary)
                    .padding(.horizontal)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .navigationTitle("SadStore")
        }
        .navigationViewStyle(.stack)
    }
}
